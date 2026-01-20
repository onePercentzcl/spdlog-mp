// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/details/periodic_worker.h>
#endif

namespace spdlog {
namespace details {

// stop the worker thread and join it
SPDLOG_INLINE periodic_worker::~periodic_worker() {
    if (worker_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
        }
        cv_.notify_one();
        worker_thread_.join();
    }
}

}  // namespace details
}  // namespace spdlog
