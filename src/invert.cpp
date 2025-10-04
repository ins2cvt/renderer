#include <windows.h>
#include <processthreadsapi.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <iostream>
#include <array>
#include <vector>
#include <memory>

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

struct Vertex
{
    glm::vec2 position;
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
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, position),
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

const std::array<Vertex, 6> middleTrapezoid = {{
    {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.4f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{0.4f, 0.5f}, {1.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
}};

const std::array<Vertex, 6> upperTrapezoid = {{
    {{0.5f, -1.0f}, {0.0f, 0.5f, 1.0f}},
    {{-0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.4f, -1.0f}, {1.0f, 0.5f, 0.0f}},
    {{0.5f, -1.0f}, {0.0f, 0.5f, 1.0f}},
    {{0.4f, 0.0f}, {1.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
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
        mainLoop();
    }

    ~Renderer()
    {
        if (device != NULL)
        {
            if (vertexBuffer != NULL)
                vkDestroyBuffer(device, vertexBuffer, NULL);
            if (vertexBufferMemory != NULL)
                vkFreeMemory(device, vertexBufferMemory, NULL);
            for (auto &fence : inflightFences)
                if (fence != NULL)
                    vkDestroyFence(device, fence, NULL);
            for (auto &semaphore : presentCompleteSemaphores)
                if (semaphore != NULL)
                    vkDestroySemaphore(device, semaphore, NULL);
            for (auto &semaphore : renderFinishedSemaphores)
                if (semaphore != NULL)
                    vkDestroySemaphore(device, semaphore, NULL);
            if (commandBuffers.size() > 0)
                vkFreeCommandBuffers(device, commandPool, MAX_FRAMES_IN_FLIGHT, commandBuffers.data());
            if (transferCommandBuffer != NULL)
                vkFreeCommandBuffers(device, commandPool, 1, &transferCommandBuffer);
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
        for (;;)
        {
            drawFrame();
        }

        vkDeviceWaitIdle(device);
    }

    void handleFramebufferResize(Dimensions dimensions)
    {
        extent = {
            .width = dimensions.width,
            .height = dimensions.height,
        };
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
        for (auto &view : swapchainImageViews)
            vkDestroyImageView(device, view, NULL);
        if (swapchain != NULL)
            vkDestroySwapchainKHR(device, swapchain, NULL);
    }

    void recreateSwapchain()
    {
        vkDeviceWaitIdle(device);

        cleanupSwapchain();
        createSwapchain();
    }

    void drawFrame()
    {
        while (vkWaitForFences(device, 1, &inflightFences[currentFrame], VK_TRUE, UINT64_MAX) == VK_TIMEOUT)
            ;

        uint32_t imageIndex = 0;

        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentCompleteSemaphores[semaphoreIndex], VK_NULL_HANDLE, &imageIndex);
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

        VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        VkRenderingAttachmentInfo attachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchainImageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearColor,
        };

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

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = scissor,
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo,
        };

        vkCmdBeginRendering(commandBuffers[currentFrame], &renderingInfo);

        vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        std::array<VkBuffer, 2> buffers = {
            vertexBuffer,
            transferBuffer,
        };
        std::array<VkDeviceSize, 2> offsets = {
            0,
            0,
        };

        vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
        vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);

        for (uint32_t i = 0; i < buffers.size(); i++)
        {
            vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, &buffers[i], &offsets[i]);
            vkCmdDraw(commandBuffers[currentFrame], 6, 1, 0, 0);
        }

        vkCmdEndRendering(commandBuffers[currentFrame]);

        transitionSwapchainImageLayout(imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(commandBuffers[currentFrame]);
    }

    // MARK: Renderer: Init Vk res

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

        vkQueueWaitIdle(transferQueue);
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
        createSwapchain();

        // Vertex  buffer creation.

        VkBufferCreateInfo vertexBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(middleTrapezoid[0]) * middleTrapezoid.size(),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        vkCreateBuffer(device, &vertexBufferInfo, NULL, &vertexBuffer);

        VkMemoryRequirements vertexMemoryRequirements = {};
        vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexMemoryRequirements);

        VkPhysicalDeviceMemoryProperties2 vertexMemoryProperties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        };

        VkMemoryAllocateInfo memoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = vertexMemoryRequirements.size,
            .memoryTypeIndex = getBufferMemoryTypeBitOrder(vertexMemoryRequirements, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)),
        };

        vkAllocateMemory(device, &memoryAllocateInfo, NULL, &vertexBufferMemory);

        vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

        void *data = nullptr;
        vkMapMemory(device, vertexBufferMemory, 0, vertexBufferInfo.size, NULL, &data);

        memcpy(data, middleTrapezoid.data(), vertexBufferInfo.size);

        vkUnmapMemory(device, vertexBufferMemory);

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

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescription();

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
            .depthBiasSlopeFactor = 1.0f,
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

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 0,
            .pushConstantRangeCount = 0,
        };

        vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

        VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapchainSurfaceFormat.format,
        };

        VkPipelineMultisampleStateCreateInfo multisamplesStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
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
            .commandBufferCount = numSwapchainImages,
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
    bool framebufferResized = false;
    VkViewport viewport = {};
    VkExtent2D extent = {};

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
    VkDeviceMemory transferBufferMemory = NULL;
    VkBuffer transferBuffer = NULL;
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
    std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>((WindowInterface *)lpParameter);

    ((Window *)lpParameter)->renderer = std::move(renderer);

    return 0;
}

// MARK: Entrypoint

int main(int argc, char *argv[])
{
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
