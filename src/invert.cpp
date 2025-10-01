#include <windows.h>

#include <vulkan/vulkan.h>

#include <iostream>
#include <array>
#include <vector>

const std::array<const char *, 1> requiredInstanceLayers = {
    "VK_LAYER_KHRONOS_validation",
};

std::array<const char *, 3> requiredInstanceExtensions = {
    "VK_KHR_surface",
    "VK_KHR_win32_surface",
    "VK_EXT_debug_utils",
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
            severityColor = "\x1b[37m";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            severityColor = "\x1b[34m";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            severityColor = "\x1b[33m";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            severityColor = "\x1b[31m";
        }

        const char *messagePrefix = NULL;

        switch (messageTypes)
        {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
            messagePrefix = "[GENERAL] ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
            messagePrefix = "[VALIDATION] ";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
            messagePrefix = "[PERFORMANCE] ";
        }

        std::format("%s%s%s\e[m\n", severityColor, messagePrefix, pCallbackData->pMessage);

        return VK_TRUE;
    }

    Renderer()
    {
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
                printf("Instance extensionnot supported: %s\n", requiredInstanceExtension);
        }

        VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = (uint32_t)requiredInstanceLayers.size(),
            .ppEnabledLayerNames = requiredInstanceLayers.data(),
            .enabledExtensionCount = (uint32_t)requiredInstanceExtensions.size(),
            .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
        };

        if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS)
            printf("Failed to create Vulkan instance\n");

        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugMessage,
        };

        if (vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, NULL, &messenger) != VK_SUCCESS)
            printf("Failed to create Vulkan Debug Utils Messenger\n");
    }

    ~Renderer()
    {
        if (instance != NULL)
        {
            if (messenger != NULL)
                vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);

            vkDestroyInstance(instance, NULL);
        }
    }

private:
    VkInstance instance = NULL;
    VkDebugUtilsMessengerEXT messenger = NULL;
};

class Window
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

private:
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        }

        return DefWindowProcA(m_hWnd, uMsg, wParam, lParam);
    }

    HWND m_hWnd = NULL;
    Renderer renderer = Renderer();
};

int main(int argc, char *argv[])
{
    printf("Hello, World!");

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
