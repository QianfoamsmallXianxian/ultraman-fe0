#ifndef LIBRETRO_VULKAN_H__
#define LIBRETRO_VULKAN_H__

#include <vulkan/vulkan.h>
#include <stdint.h>
#include "libretro.h"

#define RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION 5
#define RETRO_HW_RENDER_INTERFACE_VULKAN 0

struct retro_vulkan_image {
    VkImageView image_view;
    VkImageLayout image_layout;
    VkImageCreateInfo create_info;
};

struct retro_vulkan_context {
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    uint32_t queue_family_index;
    VkQueue queue;
    uint32_t presentation_queue_family_index;
    VkQueue presentation_queue;
};

typedef void (*retro_vulkan_set_image_t)(void *handle,
    const struct retro_vulkan_image *image,
    uint32_t num_semaphores, const VkSemaphore *semaphores,
    uint32_t src_queue_family, uint32_t dst_queue_family);

typedef uint32_t (*retro_vulkan_get_sync_index_t)(void *handle);
typedef uint32_t (*retro_vulkan_get_sync_index_mask_t)(void *handle);
typedef void (*retro_vulkan_set_command_buffers_t)(void *handle,
    uint32_t num_cmd, const VkCommandBuffer *cmd);
typedef void (*retro_vulkan_wait_sync_index_t)(void *handle);
typedef void (*retro_vulkan_lock_queue_t)(void *handle);
typedef void (*retro_vulkan_unlock_queue_t)(void *handle);
typedef void (*retro_vulkan_set_signal_semaphore_t)(void *handle, VkSemaphore sem);

struct retro_hw_render_interface_vulkan {
    enum retro_hw_render_interface_type interface_type;
    unsigned interface_version;
    void *handle;
    retro_vulkan_set_image_t set_image;
    retro_vulkan_get_sync_index_t get_sync_index;
    retro_vulkan_get_sync_index_mask_t get_sync_index_mask;
    retro_vulkan_set_command_buffers_t set_command_buffers;
    retro_vulkan_wait_sync_index_t wait_sync_index;
    retro_vulkan_lock_queue_t lock_queue;
    retro_vulkan_unlock_queue_t unlock_queue;
    retro_vulkan_set_signal_semaphore_t set_signal_semaphore;
    const struct retro_vulkan_context *context;
};

#endif
