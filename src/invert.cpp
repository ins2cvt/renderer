#include <windows.h>
#include <processthreadsapi.h>
#include <synchapi.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <array>
#include <vector>
#include <memory>
#include <chrono>
#include <utility>
#include <atomic>
#include <thread>

// MARK: Obj loader

// TODO: Make the loader work for all possible variations of the data and extract all the data. There are debug `printf` that are commented out in case this misbehaves.
struct Obj
{
    alignas(16) float *vertexData = nullptr;
    alignas(16) unsigned *indexData = nullptr;
    unsigned vertexDataSize = 0;
    unsigned indexDataSize = 0;
    unsigned numIndices = 0;

    ~Obj()
    {
        if (vertexData != nullptr)
            free(vertexData);
        if (indexData != nullptr)
            free(indexData);
    }

    // TODO: Chunked reads.
    Obj(const char *filename)
    {
        FILE *file = fopen(filename, "rb");
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, 0, SEEK_SET);
        char *buffer = (char *)malloc(size);
        fread(buffer, 1, size, file);
        fclose(file);

        unsigned numVertexLines = 0;
        unsigned numIndexLines = 0;

        unsigned offset = 0;

        while (offset < size)
        {
            if (strncmp("v", buffer + offset, 1) == 0)
            {
                numVertexLines++;
            }
            else if (strncmp("f", buffer + offset, 1) == 0)
            {
                numIndexLines++;
            }

            // Skip until seeing a newline.
            while (buffer[offset] != '\n')
            {
                offset++;
            }
            // Then skip the newline.
            offset++;
        }

        vertexDataSize = sizeof(float) * numVertexLines * 4;
        indexDataSize = sizeof(unsigned) * numIndexLines * 3;
        numIndices = numIndexLines * 3;

        // To satisfy std430 requirements for `vec3`s.
        vertexData = (float *)malloc(vertexDataSize);
        indexData = (unsigned *)malloc(indexDataSize);

        offset = 0;

        unsigned vertexDataIndex = 0;
        unsigned indexDataIndex = 0;

        while (offset < size)
        {
            if (strncmp("v", buffer + offset, 1) == 0)
            {
                offset += 2;

                for (int i = 0; i < 3; i++)
                {
                    bool positive = buffer[offset] != '-';

                    if (positive != true)
                    {
                        offset++;
                    }

                    unsigned integralPart = buffer[offset] - '0';
                    offset += 2; // Skip decimal point as well.

                    unsigned fractionalPart = 0; // As an integer.
                    unsigned multiplier = 1000000;
                    for (int j = 0; j < 7; j++)
                    {
                        fractionalPart += (buffer[offset] - '0') * multiplier;
                        multiplier /= 10;

                        offset++;
                    }

                    offset++; // Skip exponent symbol.

                    bool positiveExponent = buffer[offset] != '-';

                    if (positiveExponent != true)
                    {
                        offset++;
                    }

                    unsigned exponent = 0;
                    unsigned exponentMultiplier = 100;
                    for (int j = 0; j < 3; j++)
                    {
                        exponent += (buffer[offset] - '0') * exponentMultiplier;
                        exponentMultiplier /= 10;

                        offset++;
                    }

                    offset++; // Skip space/newline.

                    float normaliser = 1.0f;
                    for (int j = 0; j < exponent; j++)
                    {
                        if (positiveExponent)
                        {
                            normaliser *= 10.0f;
                        }
                        else
                        {
                            normaliser /= 10.0f;
                        }
                    }

                    float result = ((float)integralPart + (fractionalPart / 10000000.0f)) * normaliser * (positive ? 1.0f : -1.0f);

                    vertexData[vertexDataIndex * 4 + i] = result;
                }

                // printf("v %f %f %f\n", vertexData[vertexDataIndex * 4 + 0], vertexData[vertexDataIndex * 4 + 1], vertexData[vertexDataIndex * 4 + 2]);

                vertexDataIndex++;
            }
            else if (strncmp("f", buffer + offset, 1) == 0)
            {
                offset += 2; // Skips 'f '.

                for (int i = 0; i < 3; i++)
                {
                    unsigned digitCount = 0;
                    unsigned digits[8] = {};

                    while (buffer[offset] <= '9' && buffer[offset] >= '0')
                    {
                        digits[digitCount] = buffer[offset] - '0';

                        offset++; // Skips digits only.
                        digitCount++;
                    }

                    // Additional CRLF check due to Stanford bunny using it on index lines but not vertex lines.
                    while (buffer[offset] == ' ' || buffer[offset] == 0x0A || buffer[offset] == 0x0D)
                        offset++; // Skips space or newline.

                    unsigned result = 0;
                    unsigned multiplier = 1;

                    for (int j = 0; j < digitCount; j++)
                    {
                        result += digits[digitCount - 1 - j] * multiplier;

                        multiplier *= 10;
                    }

                    indexData[indexDataIndex * 3 + i] = result - 1;
                }

                // printf("f %u %u %u\n", indexData[indexDataIndex * 3 + 0], indexData[indexDataIndex * 3 + 1], indexData[indexDataIndex * 3 + 2]);

                indexDataIndex++;
            }
            else
            {
                while (buffer[offset] != '\n')
                {
                    offset++;
                }

                offset++;
            }
        }
    }

    VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription = {
            .binding = 0,
            .stride = sizeof(float) * 4,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        return bindingDescription;
    }

    std::array<VkVertexInputAttributeDescription, 1> getAttributeDescription()
    {
        VkVertexInputAttributeDescription positionsDescription = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 0,
        };

        std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions = {
            positionsDescription,
        };

        return attributeDescriptions;
    }
};

// MARK: Renderer frontmatter

const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

const std::array<const char *, 1> requiredInstanceLayers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::array<const char *, 3> requiredInstanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

const std::array<const char *, 4> requiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
};

struct Dimensions
{
    uint32_t width = 0;
    uint32_t height = 0;
};

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription = {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription()
    {
        VkVertexInputAttributeDescription positionsDescription = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, pos),
        };

        VkVertexInputAttributeDescription colorsDescription = {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color),
        };

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {
            positionsDescription,
            colorsDescription,
        };

        return attributeDescriptions;
    }
};

const std::array<Vertex, 6> upperTrapezoid = {{
    {{0.5f, -1.0f, 0.5f}, {0.0f, 0.5f, 1.0f}},
    {{-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.4f, -1.0f, 0.5f}, {1.0f, 0.5f, 0.0f}},
    {{0.5f, -1.0f, 0.5f}, {0.0f, 0.5f, 1.0f}},
    {{0.4f, 0.0f, 0.5f}, {1.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}},
}};

// TODO: Replace with a proper interface after factoring classes out.
class WindowInterface
{
public:
    virtual VkResult createVulkanSurface(VkInstance instance, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) = 0;
};

DWORD createRendererThread(LPVOID lpParameter);

// MARK: Renderer class

class Renderer
{
public:
    static VkResult vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
    {
        static auto f = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

        if (f == NULL)
            printf("Failed to get proc address of vkCreateDebugUtilsMessengerEXT\n");

        return f(instance, pCreateInfo, pAllocator, pMessenger);
    }

    static void vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator)
    {
        static auto f = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

        if (f == NULL)
            printf("Failed to get proc address of vkDestroyDebugUtilsMessengerEXT\n");

        return f(instance, messenger, pAllocator);
    }

    static VkBool32 debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
    {
        const char *severityColor = NULL;

        switch (messageSeverity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            severityColor = "\x1b[37m[VERBOSE: ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            severityColor = "\x1b[34m[INFO: ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            severityColor = "\x1b[33m[WARNING: ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            severityColor = "\x1b[31m[ERROR: ";
        }

        const char *messagePrefix = NULL;

        switch (messageTypes)
        {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
            messagePrefix = "GENERAL] ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
            messagePrefix = "VALIDATION] ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
            messagePrefix = "PERFORMANCE] ";
        }

        printf("%s%s\x1b[m%s\n", severityColor, messagePrefix, pCallbackData->pMessage);

        return VK_TRUE;
    }

    // MARK: Renderer ctor/dtor

    // TODO: Replace with proper interface after factoring relevant class out.
    Renderer(WindowInterface *windowInterface) : windowInterface(windowInterface)
    {
        initializeVulkan();
        initializeVulkanResources();
    }

    ~Renderer()
    {
        shouldDestruct = true;

        while (canDestruct == false)
            ;

        if (device != NULL)
        {
            if (depthImageMemory != NULL)
                vkFreeMemory(device, depthImageMemory, NULL);
            if (depthImage != NULL)
                vkDestroyImage(device, depthImage, NULL);
            for (auto &descriptorSetLayout : descriptorSetLayouts)
                if (descriptorSetLayout != NULL)
                    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
            if (descriptorPool != NULL)
                vkDestroyDescriptorPool(device, descriptorPool, NULL);
            for (auto &uniformBufferMemory : uniformBuffersMemory)
                if (uniformBufferMemory != NULL)
                {
                    vkUnmapMemory(device, uniformBufferMemory);
                    vkFreeMemory(device, uniformBufferMemory, NULL);
                }
            for (auto &uniformBuffer : uniformBuffers)
                if (uniformBuffer != NULL)
                    vkDestroyBuffer(device, uniformBuffer, NULL);
            if (vertexBuffer != NULL)
                vkDestroyBuffer(device, vertexBuffer, NULL);
            if (vertexBufferMemory != NULL)
                vkFreeMemory(device, vertexBufferMemory, NULL);
            if (indexBuffer != NULL)
                vkDestroyBuffer(device, indexBuffer, NULL);
            if (indexBufferMemory != NULL)
                vkFreeMemory(device, indexBufferMemory, NULL);
            for (auto &fence : inflightFences)
                if (fence != NULL)
                    vkDestroyFence(device, fence, NULL);
            for (auto &semaphore : presentCompleteSemaphores)
                if (semaphore != NULL)
                    vkDestroySemaphore(device, semaphore, NULL);
            for (auto &semaphore : renderFinishedSemaphores)
                if (semaphore != NULL)
                    vkDestroySemaphore(device, semaphore, NULL);
            for (auto &commandBuffer : commandBuffers)
                if (commandBuffer != NULL)
                    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            if (transferCommandBuffer != NULL)
                vkFreeCommandBuffers(device, transferCommandPool, 1, &transferCommandBuffer);
            if (commandPool != NULL)
                vkDestroyCommandPool(device, commandPool, NULL);
            if (transferCommandPool != NULL)
                vkDestroyCommandPool(device, transferCommandPool, NULL);
            if (pipeline != NULL)
                vkDestroyPipeline(device, pipeline, NULL);
            if (pipelineLayout != NULL)
                vkDestroyPipelineLayout(device, pipelineLayout, NULL);
            if (shaderModule != NULL)
                vkDestroyShaderModule(device, shaderModule, NULL);

            cleanupSwapchain();

            vkDestroyDevice(device, NULL);
        }

        if (instance != NULL)
        {
            if (surface != NULL)
                vkDestroySurfaceKHR(instance, surface, NULL);
            if (messenger != NULL)
                vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);

            vkDestroyInstance(instance, NULL);
        }
    }

    // MARK: Renderer methods

    void mainLoop()
    {
        while (shouldDestruct == false)
        {
            drawFrame();
        }

        vkDeviceWaitIdle(device);

        canDestruct = true;
    }

    void handleFramebufferResize(Dimensions dimensions)
    {
        // Ignore if the previous pending extent hasn't been picked up.
        if (framebufferResized == true)
            return;

        pendingExtent = {
            .width = dimensions.width,
            .height = dimensions.height,
        };

        printf("Framebuffer resized: %u x %u\n", dimensions.width, dimensions.height);

        framebufferResized = true;
    }

    void createSwapchain()
    {
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

        extent = surfaceCapabilities.currentExtent;

        uint32_t surfaceFormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, NULL);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data());

        for (const auto &surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
            {
                swapchainSurfaceFormat = surfaceFormat;
                break;
            }
        }

        numSwapchainImages = surfaceCapabilities.minImageCount + 1;

        if (numSwapchainImages > surfaceCapabilities.maxImageCount)
            numSwapchainImages = surfaceCapabilities.maxImageCount;

        VkSwapchainCreateInfoKHR swapchainInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = numSwapchainImages,
            .imageFormat = swapchainSurfaceFormat.format,
            .imageColorSpace = swapchainSurfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &graphicsQueueFamilyIndex,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
        };

        if (swapchainInfo.imageExtent.width == 0 || swapchainInfo.imageExtent.height == 0)
        {
            printf("Aborting swapchain creation due to dimension of 0 size\n");
            return;
        }

        if (vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain) != VK_SUCCESS)
            printf("Failed to create swapchain\n");

        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
        swapchainImages = std::vector<VkImage>(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainSurfaceFormat.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        swapchainImageViews = std::vector<VkImageView>(imageCount);

        for (uint32_t i = 0; i < imageCount; i++)
        {
            viewInfo.image = swapchainImages[i];
            vkCreateImageView(device, &viewInfo, NULL, &swapchainImageViews[i]);
        }
    }

    void cleanupSwapchain()
    {
        if (depthImageView != NULL)
        {
            vkDestroyImageView(device, depthImageView, NULL);
            depthImageView = NULL;
        }
        for (auto &view : swapchainImageViews)
        {
            vkDestroyImageView(device, view, NULL);
            view = NULL;
        }
        if (swapchain != NULL)
        {
            vkDestroySwapchainKHR(device, swapchain, NULL);
            swapchain = NULL;
        }
    }

    void recreateSwapchain()
    {
        vkDeviceWaitIdle(device);

        cleanupSwapchain();
        while (swapchain == NULL)
        {
            createSwapchain();
            if (shouldDestruct)
            {
                return;
            }
        }
        createDepthResources();
    }

    // TODO: Use a push constant for this.
    void updateUniformBuffer(uint32_t frameIndex)
    {
        static auto start = std::chrono::high_resolution_clock::now();

        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(now - start).count();

        UniformBufferObject ubo = {};

        glm::vec3 cameraPosition = glm::vec3(0.25f, 0.25f, 0.25f);
        glm::vec3 cameraFocus = glm::vec3(0.0f, 0.0f, 0.0f);
        cameraAngle = cameraFocus - cameraPosition;

        // TODO: Use the right GLM define so that angles can be input in degrees.
        ubo.model = glm::rotate(glm::mat4(1.0f), elapsed * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(cameraPosition, cameraFocus, glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(extent.width) / static_cast<float>(extent.height), 0.1f, 10.0f);

        memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }

    void drawFrame()
    {
        while (vkWaitForFences(device, 1, &inflightFences[currentFrame], VK_TRUE, UINT64_MAX) == VK_TIMEOUT)
            ;

        uint32_t imageIndex = 0;

        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentCompleteSemaphores[semaphoreIndex], VK_NULL_HANDLE, &imageIndex);

        updateUniformBuffer(currentFrame);

        vkResetFences(device, 1, &inflightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        recordCommandBuffer(imageIndex);

        VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentCompleteSemaphores[semaphoreIndex],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffers[currentFrame],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinishedSemaphores[imageIndex],
        };

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, inflightFences[currentFrame]);

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        };

        VkResult result = vkQueuePresentKHR(graphicsQueue, &presentInfo);

        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            extent = pendingExtent;
            framebufferResized = false;

            recreateSwapchain();
        }
        else if (result != VK_SUCCESS)
        {
            printf("Unexpected present error %d\n", result);
        }

        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphores.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void transitionSwapchainImageLayout(uint32_t imageIndex, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask)
    {
        VkImageMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImages[imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(commandBuffers[currentFrame], &dependencyInfo);
    }

    void recordCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

        transitionSwapchainImageLayout(imageIndex, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        transitionImageLayout(depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        VkRenderingAttachmentInfo colorAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchainImageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearColor,
        };

        VkImageMemoryBarrier2 depthBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkDependencyInfo depthDependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &depthBarrier,
        };

        vkCmdPipelineBarrier2(commandBuffers[currentFrame], &depthDependencyInfo);

        VkRect2D scissor = {
            .offset = {0, 0},
            .extent = extent,
        };

        viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)extent.width,
            .height = (float)extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkClearValue clearDepth = {1.0f, 0};

        VkRenderingAttachmentInfo depthAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depthImageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = clearDepth,
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = scissor,
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo,
        };

        vkCmdBeginRendering(commandBuffers[currentFrame], &renderingInfo);

        vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec3), &cameraAngle);

        vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
        vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);

        vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, NULL);

        VkDeviceSize offset = 0;

        vkCmdBindIndexBuffer(commandBuffers[currentFrame], indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, &vertexBuffer, &offset);
        vkCmdDrawIndexed(commandBuffers[currentFrame], stanfordBunny.numIndices, 1, 0, 0, 0);

        // vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, &transferBuffer, &offset);
        // vkCmdDraw(commandBuffers[currentFrame], 6, 1, 0, 0);

        vkCmdEndRendering(commandBuffers[currentFrame]);

        transitionSwapchainImageLayout(imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(commandBuffers[currentFrame]);
    }

    // MARK: Renderer: Init Vk res

    void transitionImageLayout(const VkImage &image, const VkFormat &format, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkCommandBuffer transitionCommandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = {
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkPipelineStageFlags sourceStage = NULL;
        VkPipelineStageFlags destinationStage = NULL;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = NULL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = NULL;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else
            printf("Invalid layout transition\n");

        if (format == VK_FORMAT_D32_SFLOAT)
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        else
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        // TODO: Use `vkCmdPipelineBarrier2`.
        vkCmdPipelineBarrier(transitionCommandBuffer, sourceStage, destinationStage, NULL, 0, NULL, 0, NULL, 1, &barrier);

        endSingleTimeCommands(std::move(transitionCommandBuffer));
    }

    // Begins a single-time command buffer on the graphics queue.
    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer singleTimeCommandBuffer = NULL;

        vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &singleTimeCommandBuffer);

        VkCommandBufferBeginInfo commandBufferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        vkBeginCommandBuffer(singleTimeCommandBuffer, &commandBufferBeginInfo);

        return singleTimeCommandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer &&singleTimeCommandBuffer)
    {
        vkEndCommandBuffer(singleTimeCommandBuffer);

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &singleTimeCommandBuffer,
        };

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, NULL);

        vkQueueWaitIdle(graphicsQueue);
    }

    void createDepthResources()
    {
        // Creating the depth image.

        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        vkCreateImage(device, &imageInfo, NULL, &depthImage);

        VkMemoryRequirements imageMemoryRequirements = {};

        vkGetImageMemoryRequirements(device, depthImage, &imageMemoryRequirements);

        VkMemoryAllocateInfo imageMemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = imageMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(imageMemoryRequirements, (VkMemoryPropertyFlagBits)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };

        vkAllocateMemory(device, &imageMemoryAllocateInfo, NULL, &depthImageMemory);

        vkBindImageMemory(device, depthImage, depthImageMemory, 0);

        // Creating the depth image view.

        VkImageViewCreateInfo depthImageViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        vkCreateImageView(device, &depthImageViewInfo, NULL, &depthImageView);
    }

    // TODO: Rework this so it's only used when necessary (i.e. when there's no graphics queue family with transfer).
    // TODO: Rework this so it can be done at draw time and more efficiently, e.g. keeping staging buffers mapped and doing the operations as part of the graphics pipeline by using barriers.
    void transferData()
    {
        std::array<uint32_t, 2> queueFamilyIndices = {
            transferQueueFamilyIndex,
            graphicsQueueFamilyIndex,
        };

        VkBufferCreateInfo stagingBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(upperTrapezoid[0]) * upperTrapezoid.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = (uint32_t)queueFamilyIndices.size(),
            .pQueueFamilyIndices = queueFamilyIndices.data(),
        };

        VkBuffer stagingBuffer = NULL;

        vkCreateBuffer(device, &stagingBufferInfo, NULL, &stagingBuffer);

        VkMemoryRequirements stagingMemoryRequirements = {};

        vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingMemoryRequirements);

        VkMemoryAllocateInfo stagingMemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = stagingMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(stagingMemoryRequirements, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)),
        };

        VkDeviceMemory stagingMemory = NULL;

        vkAllocateMemory(device, &stagingMemoryAllocateInfo, NULL, &stagingMemory);
        vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

        void *data = nullptr;

        vkMapMemory(device, stagingMemory, 0, stagingBufferInfo.size, NULL, &data);
        memcpy(data, upperTrapezoid.data(), stagingBufferInfo.size);
        vkUnmapMemory(device, stagingMemory);

        VkBufferCreateInfo destinationBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(upperTrapezoid[0]) * upperTrapezoid.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &destinationBufferInfo, NULL, &transferBuffer);
        VkMemoryRequirements destinationMemoryRequirements = {};
        vkGetBufferMemoryRequirements(device, transferBuffer, &destinationMemoryRequirements);
        VkMemoryAllocateInfo destinationMemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = destinationMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(destinationMemoryRequirements, (VkMemoryPropertyFlagBits)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkAllocateMemory(device, &destinationMemoryAllocateInfo, NULL, &transferBufferMemory);
        vkBindBufferMemory(device, transferBuffer, transferBufferMemory, 0);

        VkCommandBufferBeginInfo transferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        vkResetCommandBuffer(transferCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        vkBeginCommandBuffer(transferCommandBuffer, &transferBeginInfo);

        VkBufferCopy bufferCopyRegion = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = destinationBufferInfo.size,
        };

        vkCmdCopyBuffer(transferCommandBuffer, stagingBuffer, transferBuffer, 1, &bufferCopyRegion);

        vkEndCommandBuffer(transferCommandBuffer);

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &transferCommandBuffer,
        };

        vkQueueSubmit(transferQueue, 1, &submitInfo, NULL);

        vkDeviceWaitIdle(device);

        vkFreeMemory(device, stagingMemory, NULL);
        vkDestroyBuffer(device, stagingBuffer, NULL);
        vkFreeMemory(device, transferBufferMemory, NULL);
        vkDestroyBuffer(device, transferBuffer, NULL);
    }

    uint32_t getBufferMemoryTypeBitOrder(VkMemoryRequirements requirements, VkMemoryPropertyFlagBits requestedProperties)
    {
        const auto &properties = physicalDeviceMemoryProperties.memoryProperties;

        for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
        {
            if ((requirements.memoryTypeBits & (1 << i)) &&
                (properties.memoryTypes[i].propertyFlags & requestedProperties))
            {
                return i;
            }
        }

        return UINT32_MAX;
    }

    void initializeVulkanResources()
    {
        while (swapchain == NULL)
        {
            createSwapchain();
            if (shouldDestruct)
            {
                return;
            }
        }

        // Vertex  buffer creation.

        VkBufferCreateInfo vertexBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = stanfordBunny.vertexDataSize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        vkCreateBuffer(device, &vertexBufferInfo, NULL, &vertexBuffer);

        VkMemoryRequirements vertexMemoryRequirements = {};
        vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexMemoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = vertexMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(vertexMemoryRequirements, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)),
        };

        vkAllocateMemory(device, &memoryAllocateInfo, NULL, &vertexBufferMemory);

        vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

        void *vertexData = nullptr;
        vkMapMemory(device, vertexBufferMemory, 0, vertexBufferInfo.size, NULL, &vertexData);

        memcpy(vertexData, stanfordBunny.vertexData, vertexBufferInfo.size);

        vkUnmapMemory(device, vertexBufferMemory);

        // TODO: Deduplicate code above and below.
        // TODO: Store everything in one buffer and use offsets.

        VkBufferCreateInfo indexBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = stanfordBunny.indexDataSize,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &indexBufferInfo, NULL, &indexBuffer);
        VkMemoryRequirements indexMemoryRequirements = {};
        vkGetBufferMemoryRequirements(device, indexBuffer, &indexMemoryRequirements);
        VkMemoryAllocateInfo indexAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = indexMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(indexMemoryRequirements, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)),
        };
        vkAllocateMemory(device, &indexAllocateInfo, NULL, &indexBufferMemory);
        vkBindBufferMemory(device, indexBuffer, indexBufferMemory, 0);
        void *indexData = nullptr;
        vkMapMemory(device, indexBufferMemory, 0, indexBufferInfo.size, NULL, &indexData);
        memcpy(indexData, stanfordBunny.indexData, indexBufferInfo.size);
        vkUnmapMemory(device, indexBufferMemory);

        // Graphics pipeline creation.

        FILE *shaderFile = fopen("shader.spv", "rb");
        fseek(shaderFile, 0, SEEK_END);
        std::vector<char> shaderCode(ftell(shaderFile));
        fseek(shaderFile, 0, SEEK_SET);
        fread(shaderCode.data(), 1, shaderCode.size(), shaderFile);
        fclose(shaderFile);

        VkShaderModuleCreateInfo shaderModuleInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaderCode.size(),
            .pCode = (uint32_t *)shaderCode.data(),
        };

        if (vkCreateShaderModule(device, &shaderModuleInfo, NULL, &shaderModule) != VK_SUCCESS)
            printf("Failed to create shader module\n");

        VkPipelineShaderStageCreateInfo vertexShaderStageInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shaderModule,
            .pName = "vertexShader",
        };

        VkPipelineShaderStageCreateInfo fragmentShaderStageInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shaderModule,
            .pName = "fragmentShader",
        };

        VkPipelineShaderStageCreateInfo shaderStages[2] = {
            vertexShaderStageInfo,
            fragmentShaderStageInfo,
        };

        VkDescriptorSetLayoutBinding uboLayoutBindingInfo = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        };

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &uboLayoutBindingInfo,
        };

        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        descriptorSetLayouts.resize(MAX_FRAMES_IN_FLIGHT);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkBufferCreateInfo bufferCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = sizeof(UniformBufferObject),
                .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };

            vkCreateBuffer(device, &bufferCreateInfo, NULL, &uniformBuffers[i]);

            VkMemoryRequirements memoryRequirements = {};

            vkGetBufferMemoryRequirements(device, uniformBuffers[i], &memoryRequirements);

            VkMemoryAllocateInfo memoryAllocateInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = sizeof(UniformBufferObject),
                .memoryTypeIndex = getBufferMemoryTypeBitOrder(memoryRequirements, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)),
            };

            vkAllocateMemory(device, &memoryAllocateInfo, NULL, &uniformBuffersMemory[i]);

            vkBindBufferMemory(device, uniformBuffers[i], uniformBuffersMemory[i], 0);

            vkMapMemory(device, uniformBuffersMemory[i], 0, memoryAllocateInfo.allocationSize, NULL, &uniformBuffersMapped[i]);

            vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, NULL, &descriptorSetLayouts[i]);
        }

        VkDescriptorPoolSize descriptorPoolSize = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = 1,
            .pPoolSizes = &descriptorPoolSize,
        };

        vkCreateDescriptorPool(device, &descriptorPoolInfo, NULL, &descriptorPool);

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = (uint32_t)descriptorSetLayouts.size(),
            .pSetLayouts = descriptorSetLayouts.data(),
        };

        vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, descriptorSets.data());

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo descriptorBufferInfo = {
                .buffer = uniformBuffers[i],
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };

            VkWriteDescriptorSet writeDescriptorSet = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &descriptorBufferInfo,
            };

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
        }

        auto bindingDescription = stanfordBunny.getBindingDescription();
        auto attributeDescriptions = stanfordBunny.getAttributeDescription();

        VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size(),
            .pVertexAttributeDescriptions = attributeDescriptions.data(),
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        VkDynamicState dynamicStates[2] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicStates,
        };

        VkPipelineViewportStateCreateInfo viewportStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = NULL,
            .scissorCount = 1,
            .pScissors = NULL,
        };

        VkPipelineRasterizationStateCreateInfo rasterizationStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachmentState,
        };

        VkPushConstantRange pushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(glm::vec3),
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = (uint32_t)descriptorSetLayouts.size(),
            .pSetLayouts = descriptorSetLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange,
        };

        vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

        VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapchainSurfaceFormat.format,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        };

        VkPipelineMultisampleStateCreateInfo multisamplesStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
        };

        VkGraphicsPipelineCreateInfo graphicsPipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputStateInfo,
            .pInputAssemblyState = &inputAssemblyStateInfo,
            .pViewportState = &viewportStateInfo,
            .pRasterizationState = &rasterizationStateInfo,
            .pMultisampleState = &multisamplesStateInfo,
            .pDepthStencilState = &depthStencilStateInfo,
            .pColorBlendState = &colorBlendStateInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = pipelineLayout,
            .renderPass = NULL,
        };

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, NULL, &pipeline) != VK_SUCCESS)
            printf("Graphics pipeline creation failed\n");

        // Command pool and command buffer creation.

        VkCommandPoolCreateInfo commandPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = graphicsQueueFamilyIndex,
        };

        if (vkCreateCommandPool(device, &commandPoolInfo, NULL, &commandPool) != VK_SUCCESS)
            printf("Command pool creation failed\n");

        VkCommandPoolCreateInfo transferCommandPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = transferQueueFamilyIndex,
        };

        if (vkCreateCommandPool(device, &transferCommandPoolInfo, NULL, &transferCommandPool) != VK_SUCCESS)
            printf("Transfer command pool creation failed\n");

        VkCommandBufferAllocateInfo commandBufferAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
        };

        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        if (vkAllocateCommandBuffers(device, &commandBufferAllocInfo, commandBuffers.data()) != VK_SUCCESS)
            printf("Failed to allocate command buffers\n");

        VkCommandBufferAllocateInfo transferCommandBufferAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = transferCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        if (vkAllocateCommandBuffers(device, &transferCommandBufferAllocInfo, &transferCommandBuffer) != VK_SUCCESS)
            printf("Failed to allocate transfer command buffer\n");

        // Create sync objects.

        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        presentCompleteSemaphores.resize(numSwapchainImages);
        renderFinishedSemaphores.resize(numSwapchainImages);
        inflightFences.resize(MAX_FRAMES_IN_FLIGHT);

        for (uint32_t i = 0; i < numSwapchainImages; i++)
        {
            VkResult res1 = vkCreateSemaphore(device, &semaphoreInfo, NULL, &presentCompleteSemaphores[i]);
            VkResult res2 = vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphores[i]);

            if ((res1 & res2) != VK_SUCCESS)
                printf("Semaphore creation failed\n");
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            if (vkCreateFence(device, &fenceInfo, NULL, &inflightFences[i]) != VK_SUCCESS)
                printf("Fence creation failed\n");

        transferData();
        createDepthResources();
    }

    // MARK: Renderer: Init Vk

    void initializeVulkan()
    {
        // Instance

        VkApplicationInfo applicationInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_4,
        };

        uint32_t instanceLayerPropertyCount = 0;
        vkEnumerateInstanceLayerProperties(&instanceLayerPropertyCount, NULL);
        std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerPropertyCount);
        vkEnumerateInstanceLayerProperties(&instanceLayerPropertyCount, instanceLayerProperties.data());

        for (const auto &requiredInstanceLayer : requiredInstanceLayers)
        {
            bool supported = false;

            for (const auto &instanceLayerProperty : instanceLayerProperties)
            {
                if (strcmp(requiredInstanceLayer, instanceLayerProperty.layerName) == 0)
                {
                    supported = true;
                    break;
                }
            }

            if (supported == false)
                printf("Instance layer not supported: %s\n", requiredInstanceLayer);
        }

        uint32_t instanceExtensionPropertyCount = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &instanceExtensionPropertyCount, NULL);
        std::vector<VkExtensionProperties> instanceExtensionProperties(instanceExtensionPropertyCount);
        vkEnumerateInstanceExtensionProperties(NULL, &instanceExtensionPropertyCount, instanceExtensionProperties.data());

        for (const auto &requiredInstanceExtension : requiredInstanceExtensions)
        {
            bool supported = false;

            for (const auto &instanceExtensionProperty : instanceExtensionProperties)
            {
                if (strcmp(requiredInstanceExtension, instanceExtensionProperty.extensionName) == 0)
                {
                    supported = true;
                    break;
                }
            }

            if (supported == false)
                printf("Instance extension not supported: %s\n", requiredInstanceExtension);
        }

        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugMessage,
        };

        VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = &messengerInfo,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = (uint32_t)requiredInstanceLayers.size(),
            .ppEnabledLayerNames = requiredInstanceLayers.data(),
            .enabledExtensionCount = (uint32_t)requiredInstanceExtensions.size(),
            .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
        };

        if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS)
            printf("Failed to create Vulkan instance\n");

        if (vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, NULL, &messenger) != VK_SUCCESS)
            printf("Failed to create Vulkan Debug Utils Messenger\n");

        // Physical device

        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

        for (const auto &physicalDeviceCandidate : physicalDevices)
        {
            // Queue with graphics and present support

            uint32_t queueFamilyPropertyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(physicalDeviceCandidate, &queueFamilyPropertyCount, NULL);
            const VkQueueFamilyProperties2 emptyQueueFamilyProperties = {
                .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
            };
            std::vector<VkQueueFamilyProperties2> queueFamilyProperties(queueFamilyPropertyCount, emptyQueueFamilyProperties);
            vkGetPhysicalDeviceQueueFamilyProperties2(physicalDeviceCandidate, &queueFamilyPropertyCount, queueFamilyProperties.data());

            graphicsQueueFamilyIndex = UINT32_MAX;

            for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
            {
                const auto &queueFamilyProperty = queueFamilyProperties[i].queueFamilyProperties;

                if (queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT && graphicsQueueFamilyIndex == UINT32_MAX)
                {
                    if (vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDeviceCandidate, i) == VK_FALSE)
                        continue;

                    graphicsQueueFamilyIndex = i;
                }

                if ((queueFamilyProperty.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    ((queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) &&
                    (transferQueueFamilyIndex == UINT32_MAX))
                {
                    transferQueueFamilyIndex = i;
                }
            }

            // API version

            VkPhysicalDeviceProperties2 physicalDeviceProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            };
            vkGetPhysicalDeviceProperties2(physicalDeviceCandidate, &physicalDeviceProperties);

            bool supportsVulkan1_4 = physicalDeviceProperties.properties.apiVersion >= VK_API_VERSION_1_4;

            // Logical device and queues

            if ((graphicsQueueFamilyIndex != UINT32_MAX) && supportsVulkan1_4)
            {
                physicalDevice = physicalDeviceCandidate;

                vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &physicalDeviceMemoryProperties);

                float queuePriorities = 0.0f;
                VkDeviceQueueCreateInfo graphicsQueueInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = graphicsQueueFamilyIndex,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriorities,
                };

                VkDeviceQueueCreateInfo transferQueueInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = transferQueueFamilyIndex,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriorities,
                };

                std::array<VkDeviceQueueCreateInfo, 2> deviceQueueInfos = {
                    graphicsQueueInfo,
                    transferQueueInfo,
                };

                VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicFeatures = {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
                    .extendedDynamicState = VK_TRUE,
                };

                VkPhysicalDeviceVulkan11Features deviceFeatures11 = {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                    .pNext = &dynamicFeatures,
                    .shaderDrawParameters = VK_TRUE,
                };

                VkPhysicalDeviceVulkan13Features deviceFeatures13 = {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                    .pNext = &deviceFeatures11,
                    .synchronization2 = VK_TRUE,
                    .dynamicRendering = VK_TRUE,
                };

                VkPhysicalDeviceFeatures2 features = {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .pNext = &deviceFeatures13,
                };

                VkDeviceCreateInfo deviceInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    .pNext = &features,
                    .queueCreateInfoCount = (uint32_t)deviceQueueInfos.size(),
                    .pQueueCreateInfos = deviceQueueInfos.data(),
                    .enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size(),
                    .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
                };

                if (vkCreateDevice(physicalDevice, &deviceInfo, NULL, &device) != VK_SUCCESS)
                    printf("Failed to create logical device\n");

                break;
            }
            else
            {
                if (supportsVulkan1_4 == false)
                    printf("%s does not support Vulkan 1.4\n", physicalDeviceProperties.properties.deviceName);

                if (graphicsQueueFamilyIndex == UINT32_MAX)
                    printf("%s does not expose a queue with both graphics and present support\n", physicalDeviceProperties.properties.deviceName);
            }
        }

        if (windowInterface->createVulkanSurface(instance, NULL, &surface) != VK_SUCCESS)
            printf("Failed to create surface\n");

        VkBool32 isSurfaceSupportedByPhysicalDevice = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueFamilyIndex, surface, &isSurfaceSupportedByPhysicalDevice);

        vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);

        if (transferQueueFamilyIndex != UINT32_MAX)
            vkGetDeviceQueue(device, transferQueueFamilyIndex, 0, &transferQueue);

        if (isSurfaceSupportedByPhysicalDevice == VK_FALSE)
            printf("Surface is not supported by physical device\n");
    }

private:
    // MARK: Renderer members

    // TODO: Replace with proper interface after factoring parent class out.
    WindowInterface *windowInterface = nullptr;
    std::atomic<bool> shouldDestruct = false;
    std::atomic<bool> canDestruct = false;

    VkInstance instance = NULL;
    VkDebugUtilsMessengerEXT messenger = NULL;
    VkSurfaceKHR surface = NULL;

    VkPhysicalDevice physicalDevice = NULL;
    VkDevice device = NULL;
    VkPhysicalDeviceMemoryProperties2 physicalDeviceMemoryProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    VkQueue graphicsQueue = NULL;
    uint32_t transferQueueFamilyIndex = UINT32_MAX;
    VkQueue transferQueue = NULL;

    VkSwapchainKHR swapchain = NULL;
    uint32_t numSwapchainImages = 0;
    std::vector<VkImage> swapchainImages = {};
    std::vector<VkImageView> swapchainImageViews = {};
    VkSurfaceFormatKHR swapchainSurfaceFormat = {};
    std::atomic<bool> framebufferResized = false;
    VkViewport viewport = {};
    VkExtent2D extent = {};
    VkExtent2D pendingExtent = {};

    VkImage depthImage = NULL;
    VkDeviceMemory depthImageMemory = NULL;
    VkImageView depthImageView = NULL;

    VkShaderModule shaderModule = NULL;
    VkPipelineLayout pipelineLayout = NULL;
    VkPipeline pipeline = NULL;

    VkCommandPool commandPool = NULL;
    std::vector<VkCommandBuffer> commandBuffers = {};
    std::vector<VkSemaphore> presentCompleteSemaphores = {};
    std::vector<VkSemaphore> renderFinishedSemaphores = {};
    std::vector<VkFence> inflightFences = {};
    uint32_t currentFrame = 0;
    uint32_t semaphoreIndex = 0;

    VkCommandPool transferCommandPool = NULL;
    VkCommandBuffer transferCommandBuffer = NULL;

    VkDeviceMemory vertexBufferMemory = NULL;
    VkBuffer vertexBuffer = NULL;
    VkDeviceMemory indexBufferMemory = NULL;
    VkBuffer indexBuffer = NULL;
    VkDeviceMemory transferBufferMemory = NULL;
    VkBuffer transferBuffer = NULL;

    std::vector<VkBuffer> uniformBuffers = {};
    std::vector<VkDeviceMemory> uniformBuffersMemory = {};
    std::vector<void *> uniformBuffersMapped = {};

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {};
    VkDescriptorPool descriptorPool = NULL;
    std::vector<VkDescriptorSet> descriptorSets = {};

    Obj stanfordBunny = Obj("./res/bunny.obj");

    glm::vec3 cameraAngle = {};
};

// MARK: Window class

class Window : WindowInterface
{
public:
    Window(HINSTANCE hInstance)
    {
        WNDCLASSEXA windowClassInfo = {
            .cbSize = sizeof(WNDCLASSEX),
            .lpfnWndProc = Window::WindowProc,
            .hInstance = hInstance,
            .lpszClassName = "Window",
        };

        ATOM windowClass = RegisterClassExA(&windowClassInfo);

        HWND windowHandle = CreateWindowExA(NULL, MAKEINTATOM(windowClass), "Invert", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, this);

        ShowWindow(windowHandle, SW_NORMAL);
    }

    ~Window() {
        renderer.reset();

        if (rendererThread != NULL)
        {
            WaitForSingleObject(rendererThread, INFINITE);
            CloseHandle(rendererThread);
        }
    }

    static LRESULT WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        Window *pThis = NULL;

        if (uMsg == WM_NCCREATE)
        {
            CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
            pThis = (Window *)pCreate->lpCreateParams;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);

            pThis->m_hWnd = hWnd;
        }
        else
        {
            pThis = (Window *)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
        }

        if (pThis)
        {
            return pThis->handleMessage(uMsg, wParam, lParam);
        }
        else
        {
            return DefWindowProcA(hWnd, uMsg, wParam, lParam);
        }
    }

    VkResult createVulkanSurface(VkInstance instance, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) override
    {
        HINSTANCE hInstance = NULL;
        GetModuleHandleExA(NULL, NULL, &hInstance);

        VkWin32SurfaceCreateInfoKHR win32SurfaceInfo = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = hInstance,
            .hwnd = m_hWnd,
        };

        return vkCreateWin32SurfaceKHR(instance, &win32SurfaceInfo, pAllocator, pSurface);
    }

    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CREATE:
            rendererThread = CreateThread(NULL, NULL, createRendererThread, this, NULL, NULL);
            break;
        case WM_SIZE:
            if (renderer != nullptr && wParam != SIZE_MINIMIZED)
                renderer->handleFramebufferResize({.width = LOWORD(lParam), .height = HIWORD(lParam)});
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        }

        return DefWindowProcA(m_hWnd, uMsg, wParam, lParam);
    }

    HWND m_hWnd = NULL;

    HANDLE rendererThread = NULL;
    std::unique_ptr<Renderer> renderer = nullptr;
};

DWORD createRendererThread(LPVOID lpParameter)
{
    Window *window = (Window *)lpParameter;

    window->renderer = std::move(std::make_unique<Renderer>((WindowInterface *)lpParameter));

    window->renderer->mainLoop();

    return 0;
}

// MARK: Entrypoint

int main(int argc, char *argv[])
{
    Obj("res/bunny.obj");

    printf("Hello, World!\n");

    HINSTANCE hInstance = NULL;
    GetModuleHandleExA(NULL, NULL, &hInstance);

    Window window = Window(hInstance);

    MSG msg = {};
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
