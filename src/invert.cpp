#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <iostream>
#include <array>
#include <vector>

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

// TODO: Replace with a proper interface after factoring classes out.
class WindowInterface
{
public:
    virtual VkResult createVulkanSurface(VkInstance instance, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) = 0;
    virtual Dimensions getDimensions() = 0;
};

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

    // TODO: Replace with proper interface after factoring relevant class out.
    Renderer(WindowInterface *windowInterface) : windowInterface(windowInterface) {}

    ~Renderer()
    {

        if (device != NULL)
        {
            if (drawFence != NULL)
                vkDestroyFence(device, drawFence, NULL);
            if (presentCompleteSemaphore != NULL)
                vkDestroySemaphore(device, presentCompleteSemaphore, NULL);
            if (renderFinishedSemaphore != NULL)
                vkDestroySemaphore(device, renderFinishedSemaphore, NULL);
            if (commandPool != NULL)
                vkDestroyCommandPool(device, commandPool, NULL);
            if (shaderModule != NULL)
                vkDestroyShaderModule(device, shaderModule, NULL);
            if (pipeline != NULL)
                vkDestroyPipeline(device, pipeline, NULL);
            if (pipelineLayout != NULL)
                vkDestroyPipelineLayout(device, pipelineLayout, NULL);
            for (auto &view : swapchainImageViews)
                vkDestroyImageView(device, view, NULL);
            if (swapchain != NULL)
                vkDestroySwapchainKHR(device, swapchain, NULL);

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

    void initialize()
    {
        initializeVulkan();
        initializeVulkanResources();
        mainLoop();
    }

    void mainLoop()
    {
        for (;;)
        {
            drawFrame();
            break;
        }

        vkDeviceWaitIdle(device);
    }

    void drawFrame()
    {
        vkQueueWaitIdle(queue);

        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);
        recordCommandBuffer();

        vkResetFences(device, 1, &drawFence);

        VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentCompleteSemaphore,
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinishedSemaphore,
        };

        vkQueueSubmit(queue, 1, &submitInfo, drawFence);

        while (vkWaitForFences(device, 1, &drawFence, VK_TRUE, UINT32_MAX))
            ;

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        };

        if (vkQueuePresentKHR(queue, &presentInfo) != VK_SUCCESS)
            printf("Presentation failed\n");
    }

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

            queueFamilyIndex = UINT32_MAX;

            for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
            {
                const auto &queueFamilyProperty = queueFamilyProperties[i].queueFamilyProperties;

                if (queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDeviceCandidate, i) == VK_FALSE)
                        continue;

                    queueFamilyIndex = i;
                    break;
                }
            }

            // API version

            VkPhysicalDeviceProperties2 physicalDeviceProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            };
            vkGetPhysicalDeviceProperties2(physicalDeviceCandidate, &physicalDeviceProperties);

            bool supportsVulkan1_4 = physicalDeviceProperties.properties.apiVersion >= VK_API_VERSION_1_4;

            // Logical device and queues

            if ((queueFamilyIndex != UINT32_MAX) && supportsVulkan1_4)
            {
                physicalDevice = physicalDeviceCandidate;

                float queuePriorities = 0.0f;
                VkDeviceQueueCreateInfo deviceQueueInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = queueFamilyIndex,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriorities,
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
                    .queueCreateInfoCount = 1,
                    .pQueueCreateInfos = &deviceQueueInfo,
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

                if (queueFamilyIndex == UINT32_MAX)
                    printf("%s does not expose a queue with both graphics and present support\n", physicalDeviceProperties.properties.deviceName);
            }
        }

        if (windowInterface->createVulkanSurface(instance, NULL, &surface) != VK_SUCCESS)
            printf("Failed to create surface\n");

        VkBool32 isSurfaceSupportedByPhysicalDevice = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &isSurfaceSupportedByPhysicalDevice);

        vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

        if (isSurfaceSupportedByPhysicalDevice == VK_FALSE)
            printf("Surface is not supported by physical device\n");
    }

    void transitionSwapchainImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask)
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
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void recordCommandBuffer()
    {
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        transitionSwapchainImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

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

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = scissor,
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo,
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        vkCmdDraw(commandBuffer, 6, 1, 0, 0);

        vkCmdEndRendering(commandBuffer);

        transitionSwapchainImageLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(commandBuffer);
    }

    void initializeVulkanResources()
    {
        // Swapchain creation.

        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

        if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
        {
            extent = surfaceCapabilities.currentExtent;
        }
        else
        {
            Dimensions dimensions = windowInterface->getDimensions();
            extent = {
                .width = dimensions.width,
                .height = dimensions.height,
            };
        }

        uint32_t surfaceFormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, NULL);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data());

        VkSurfaceFormatKHR swapchainSurfaceFormat = {};

        for (const auto &surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
            {
                swapchainSurfaceFormat = surfaceFormat;
                break;
            }
        }

        uint32_t minImageCount = surfaceCapabilities.minImageCount + 1;

        if (minImageCount > surfaceCapabilities.maxImageCount)
            minImageCount = surfaceCapabilities.maxImageCount;

        VkSwapchainCreateInfoKHR swapchainInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = minImageCount,
            .imageFormat = swapchainSurfaceFormat.format,
            .imageColorSpace = swapchainSurfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &queueFamilyIndex,
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

        VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = NULL,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = NULL,
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)extent.width,
            .height = (float)extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
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
            .queueFamilyIndex = queueFamilyIndex,
        };

        if (vkCreateCommandPool(device, &commandPoolInfo, NULL, &commandPool) != VK_SUCCESS)
            printf("Command pool creation failed\n");

        VkCommandBufferAllocateInfo commandBufferAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        if (vkAllocateCommandBuffers(device, &commandBufferAllocInfo, &commandBuffer) != VK_SUCCESS)
            printf("Failed to allocate command buffer\n");

        // Sync object creation.

        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        vkCreateSemaphore(device, &semaphoreInfo, NULL, &presentCompleteSemaphore);
        vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphore);

        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        vkCreateFence(device, &fenceInfo, NULL, &drawFence);
    }

private:
    // TODO: Replace with proper interface after factoring parent class out.
    WindowInterface *windowInterface = nullptr;

    VkInstance instance = NULL;
    VkDebugUtilsMessengerEXT messenger = NULL;
    VkSurfaceKHR surface = NULL;

    VkPhysicalDevice physicalDevice = NULL;
    VkDevice device = NULL;
    // Graphics and present queue family index.
    uint32_t queueFamilyIndex = UINT32_MAX;
    VkQueue queue = NULL;

    VkSwapchainKHR swapchain = NULL;
    std::vector<VkImage> swapchainImages = {};
    std::vector<VkImageView> swapchainImageViews = {};
    uint32_t imageIndex = 0;

    VkShaderModule shaderModule = NULL;
    VkPipelineLayout pipelineLayout = NULL;
    VkPipeline pipeline = NULL;

    VkCommandPool commandPool = NULL;
    VkCommandBuffer commandBuffer = NULL;

    VkSemaphore presentCompleteSemaphore = NULL;
    VkSemaphore renderFinishedSemaphore = NULL;
    VkFence drawFence = NULL;

    VkViewport viewport = {};
    VkExtent2D extent = {};
};

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

    Dimensions getDimensions() override
    {
        RECT rect = {};
        GetClientRect(m_hWnd, &rect);

        Dimensions result = {
            .width = (uint32_t)rect.right,
            .height = (uint32_t)rect.bottom,
        };

        return result;
    }

private:
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CREATE:
            renderer.initialize();
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        }

        return DefWindowProcA(m_hWnd, uMsg, wParam, lParam);
    }

    HWND m_hWnd = NULL;

    Renderer renderer = Renderer(this);
};

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
