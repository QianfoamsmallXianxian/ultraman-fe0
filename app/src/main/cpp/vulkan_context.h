#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <cstdint>
#include "libretro_vulkan.h"

bool vk_init(ANativeWindow* win, uint32_t width, uint32_t height);
void vk_shutdown();
bool vk_recreate_swapchain(ANativeWindow* win, uint32_t width, uint32_t height);
bool vk_prepare_frame();
bool vk_render_frame();

struct retro_hw_render_interface_vulkan* vk_get_retro_interface();

#endif
