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

const std::array<const char *, 1> requiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
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

        printf("%s%s%s\x1b[m\n", severityColor, messagePrefix, pCallbackData->pMessage);

        return VK_TRUE;
    }

    // TODO: Replace with proper interface after factoring relevant class out.
    Renderer(WindowInterface *windowInterface) : windowInterface(windowInterface) {}

    void createSwapchain()
    {
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

        VkExtent2D extent = {};

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
            .pQueueFamilyIndices = &familyIndex,
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

    void initialize()
    {
        initializeVulkan();
        createSwapchain();
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

            familyIndex = UINT32_MAX;

            for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
            {
                const auto &queueFamilyProperty = queueFamilyProperties[i].queueFamilyProperties;

                if (queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDeviceCandidate, i) == VK_FALSE)
                        continue;

                    familyIndex = i;
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

            if ((familyIndex != UINT32_MAX) && supportsVulkan1_4)
            {
                physicalDevice = physicalDeviceCandidate;

                float queuePriorities = 0.0f;
                VkDeviceQueueCreateInfo deviceQueueInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = familyIndex,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriorities,
                };

                VkDeviceCreateInfo deviceInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
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

                if (familyIndex == UINT32_MAX)
                    printf("%s does not expose a queue with both graphics and present support\n", physicalDeviceProperties.properties.deviceName);
            }
        }

        if (windowInterface->createVulkanSurface(instance, NULL, &surface) != VK_SUCCESS)
            printf("Failed to create surface\n");

        VkBool32 isSurfaceSupportedByPhysicalDevice = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, surface, &isSurfaceSupportedByPhysicalDevice);

        if (isSurfaceSupportedByPhysicalDevice == VK_FALSE)
            printf("Surface is not supported by physical device\n");
    }

    ~Renderer()
    {
        if (device != NULL)
        {
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

private:
    // TODO: Replace with proper interface after factoring parent class out.
    WindowInterface *windowInterface = nullptr;

    VkInstance instance = NULL;
    VkDebugUtilsMessengerEXT messenger = NULL;
    VkSurfaceKHR surface = NULL;

    VkPhysicalDevice physicalDevice = NULL;
    VkDevice device = NULL;
    // Graphics and present queue family index.
    uint32_t familyIndex = UINT32_MAX;

    VkSwapchainKHR swapchain = NULL;
    std::vector<VkImage> swapchainImages = {};
    std::vector<VkImageView> swapchainImageViews = {};
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
