#include "device_api.hpp"
#include "private_data.hpp"
#include "wsi/display.hpp"
#include "alvr_server/Settings.h"

#include <vector>

static const char *alvr_display_name = "ALVR display";

const struct {
} alvr_display;
const VkDisplayKHR alvr_display_handle = (VkDisplayKHR_T *)&alvr_display;

const struct {
} alvr_display_mode;
const VkDisplayModeKHR alvr_display_mode_handle = (VkDisplayModeKHR_T *)&alvr_display_mode;

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkGetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice device, uint32_t *pPropertyCount, VkDisplayPropertiesKHR *pProperties) {
    auto &instance = layer::instance_private_data::get(device);
    uint32_t initial_devices = *pPropertyCount;
    VkDisplayPropertiesKHR *initialProperties = pProperties;
    VkResult result =
        instance.disp.GetPhysicalDeviceDisplayPropertiesKHR(device, pPropertyCount, pProperties);
    if (*pPropertyCount >= initial_devices) {
        *pPropertyCount += 1;
        return VK_INCOMPLETE;
    }
    if (initialProperties) {
        auto &properties = pProperties[*pPropertyCount];
        properties.display = alvr_display_handle;
        properties.displayName = alvr_display_name;
        properties.physicalDimensions = VkExtent2D{20, 20};
        properties.physicalResolution = VkExtent2D{Settings::Instance().m_renderWidth, Settings::Instance().m_renderHeight};
        properties.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        properties.planeReorderPossible = VK_FALSE;
        properties.persistentContent = VK_TRUE;
    }
    *pPropertyCount += 1;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkGetDisplayModePropertiesKHR(
    VkPhysicalDevice device, VkDisplayKHR display, uint32_t *pPropertyCount,
    VkDisplayModePropertiesKHR *pProperties) {
    auto &instance = layer::instance_private_data::get(device);

    if (display == alvr_display_handle) {
        if (*pPropertyCount == 0 and pProperties)
            return VK_INCOMPLETE;
        if (pProperties) {
            pProperties[0].displayMode = alvr_display_mode_handle;
            pProperties[0].parameters.visibleRegion = VkExtent2D{Settings::Instance().m_renderWidth, Settings::Instance().m_renderHeight};
            pProperties[0].parameters.refreshRate = Settings::Instance().m_refreshRate * 1000;
        }
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    return instance.disp.GetDisplayModePropertiesKHR(device, display, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice device, uint32_t *pPropertyCount, VkDisplayPlanePropertiesKHR *pProperties) {
    auto &instance = layer::instance_private_data::get(device);
    uint32_t initial_devices = *pPropertyCount;
    VkDisplayPlanePropertiesKHR *initialProperties = pProperties;
    VkResult result = instance.disp.GetPhysicalDeviceDisplayPlanePropertiesKHR(
        device, pPropertyCount, pProperties);

    instance.first_plane_index = *pPropertyCount;

    for (uint32_t plane = 0; plane < instance.num_planes; ++plane) {
        if (*pPropertyCount >= initial_devices) {
            return VK_INCOMPLETE;
        }
        if (initialProperties) {
            pProperties[*pPropertyCount].currentDisplay = alvr_display_handle;
            pProperties[*pPropertyCount].currentStackIndex = plane;
        }
        *pPropertyCount += 1;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkAcquireXlibDisplayEXT(VkPhysicalDevice device,
                                                                 Display *dpy,
                                                                 VkDisplayKHR display) {
    if (display == alvr_display_handle)
        return VK_SUCCESS;

    auto &instance = layer::instance_private_data::get(device);
    return instance.disp.AcquireXlibDisplayEXT(device, dpy, display);
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkGetDisplayPlaneSupportedDisplaysKHR(
    VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t *pDisplayCount,
    VkDisplayKHR *pDisplays) {
    auto &instance = layer::instance_private_data::get(physicalDevice);
    if (planeIndex >= instance.first_plane_index and
        planeIndex < instance.first_plane_index + instance.num_planes) {
        *pDisplayCount = 1;
        if (pDisplays) {
            pDisplays[0] = alvr_display_handle;
        }
        return VK_SUCCESS;
    }

    return instance.disp.GetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex,
                                                             pDisplayCount, pDisplays);
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkCreateDisplayPlaneSurfaceKHR(
    VkInstance vkinstance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    auto &instance = layer::instance_private_data::get(vkinstance);
    VkHeadlessSurfaceCreateInfoEXT createInfo{
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    auto res =
        instance.disp.CreateHeadlessSurfaceEXT(vkinstance, &createInfo, pAllocator, pSurface);
    if (*pSurface == NULL)
        std::abort();
    instance.add_surface(*pSurface);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkReleaseDisplayEXT(VkPhysicalDevice physicalDevice,
                                                             VkDisplayKHR display) {
    if (display == alvr_display_handle)
        return VK_SUCCESS;
    auto &instance = layer::instance_private_data::get(physicalDevice);
    return instance.disp.ReleaseDisplayEXT(physicalDevice, display);
}

VKAPI_ATTR void VKAPI_CALL wsi_layer_vkDestroySurfaceKHR(VkInstance vkinstance,
                                                         VkSurfaceKHR surface,
                                                         const VkAllocationCallbacks *pAllocator) {
    auto &instance = layer::instance_private_data::get(vkinstance);
    if (instance.should_layer_handle_surface(surface)) {
        return;
    }
    return instance.disp.DestroySurfaceKHR(vkinstance, surface, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkRegisterDisplayEventEXT(
    VkDevice device, VkDisplayKHR display, const VkDisplayEventInfoEXT *pDisplayEventInfo,
    const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    auto &instance = layer::device_private_data::get(device);
    if (display != alvr_display_handle) {
        return instance.disp.RegisterDisplayEventEXT(device, display, pDisplayEventInfo, pAllocator,
                                                     pFence);
    }

    *pFence = wsi::display::get().get_vsync_fence();

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkWaitForFences(VkDevice device, uint32_t fenceCount,
                                                         const VkFence *pFences, VkBool32 waitAll,
                                                         uint64_t timeout) {
    auto &instance = layer::device_private_data::get(device);
    auto &alvr_fence = wsi::display::get().get_vsync_fence();
    bool contains_alvr_fence = false;
    std::vector<VkFence> other_fences;
    for (uint32_t fence = 0; fence < fenceCount; ++fence) {
        if (pFences[fence] == alvr_fence) {
            contains_alvr_fence = true;
        } else {
            other_fences.push_back(pFences[fence]);
        }
    }

    auto until = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);

    if (waitAll) {
        if (not other_fences.empty()) {
            auto ret = instance.disp.WaitForFences(device, other_fences.size(), other_fences.data(),
                                                   waitAll, timeout);
            if (ret != VK_SUCCESS)
                return ret;
        }
        if (contains_alvr_fence) {
            bool signaled = alvr_fence.wait(until);
            if (not signaled)
                return VK_TIMEOUT;
        }
        return VK_SUCCESS;
    } else {
        // let's hope we don't go this path
        while (std::chrono::steady_clock::now() < until) {
            auto ret = instance.disp.WaitForFences(device, other_fences.size(), other_fences.data(),
                                                   waitAll, 0);
            if (ret != VK_TIMEOUT)
                return ret;
            if (alvr_fence.get())
                return VK_SUCCESS;
        }
        return VK_TIMEOUT;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL wsi_layer_vkGetFenceStatus(VkDevice device, VkFence fence) {
    auto &instance = layer::device_private_data::get(device);
    auto &alvr_fence = wsi::display::get().get_vsync_fence();
    if (fence == alvr_fence) {
        return alvr_fence.get() ? VK_SUCCESS : VK_NOT_READY;
    }
    return instance.disp.GetFenceStatus(device, fence);
}

VKAPI_ATTR void VKAPI_CALL wsi_layer_vkDestroyFence(VkDevice device, VkFence fence,
                                                    const VkAllocationCallbacks *pAllocator) {
    auto &instance = layer::device_private_data::get(device);
    auto &alvr_fence = wsi::display::get().get_vsync_fence();
    if (fence == alvr_fence) {
        return;
    }
    instance.disp.GetFenceStatus(device, fence);
}
}
