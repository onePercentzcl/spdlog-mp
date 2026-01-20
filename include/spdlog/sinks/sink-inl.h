// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/sinks/sink.h>
#endif

#include <spdlog/common.h>

SPDLOG_INLINE bool spdlog::sinks::sink::should_log(spdlog::level::level_enum msg_level) const {
    return msg_level >= level_.load(std::memory_order_relaxed);
}

SPDLOG_INLINE void spdlog::sinks::sink::set_level(level::level_enum log_level) {
    level_.store(log_level, std::memory_order_relaxed);
}

SPDLOG_INLINE spdlog::level::level_enum spdlog::sinks::sink::level() const {
    return static_cast<spdlog::level::level_enum>(level_.load(std::memory_order_relaxed));
}
