#pragma once

#include <atomic>
#include <condition_variable>
#include <vulkan/vulkan.h>
#include <thread>

namespace layer
{
class device_private_data;
}

namespace wsi {

class display {
  public:
    display(VkDevice device, layer::device_private_data& device_data, uint32_t queue_family_index, uint32_t queue_index);
    ~display();

    VkFence &get_vsync_fence() { return vsync_fence; }

    // condition variable that is signaled once per vsync
    std::condition_variable_any m_cond;
    std::atomic<uint64_t> m_vsync_count{0};

    void pixel_out();
  private:
    std::atomic_bool m_exiting{false};
    std::thread m_vsync_thread;
    VkFence vsync_fence;
};

} // namespace wsi