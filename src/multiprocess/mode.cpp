// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/mode.h>

namespace spdlog {
namespace multiprocess {

// 初始化静态成员变量（默认启用多进程模式）
std::atomic<bool> MultiprocessMode::enabled_{true};

} // namespace multiprocess
} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
