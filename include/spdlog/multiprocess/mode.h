// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file mode.h
 * @brief 多进程模式管理 - 支持运行时启用/禁用多进程模式
 * 
 * @details
 * 此文件提供了多进程模式的运行时管理功能。
 * 当禁用多进程模式时，spdlog将使用原始的日志行为。
 * 
 * 使用示例：
 * @code
 * #include "spdlog/multiprocess/mode.h"
 * 
 * // 检查多进程模式是否启用
 * if (spdlog::multiprocess::is_enabled()) {
 *     // 使用多进程日志
 * }
 * 
 * // 禁用多进程模式
 * spdlog::multiprocess::disable();
 * 
 * // 重新启用多进程模式
 * spdlog::multiprocess::enable();
 * @endcode
 */

#include <atomic>

namespace spdlog {
namespace multiprocess {

/**
 * @brief 多进程模式管理类
 * 
 * @details
 * 提供运行时启用/禁用多进程模式的功能。
 * 当禁用多进程模式时：
 * - SharedMemoryProducerSink 将使用回退sink（如果配置了）
 * - 或者直接丢弃消息（如果没有配置回退sink）
 * 
 * 这允许在不重新编译的情况下切换日志行为。
 */
class MultiprocessMode {
public:
    /**
     * @brief 检查多进程模式是否启用
     * @return 如果多进程模式启用返回true，否则返回false
     */
    static bool is_enabled() {
#ifdef SPDLOG_ENABLE_MULTIPROCESS
        return enabled_.load(std::memory_order_acquire);
#else
        return false;
#endif
    }
    
    /**
     * @brief 启用多进程模式
     * 
     * @details
     * 启用多进程模式后，SharedMemoryProducerSink将尝试写入共享内存。
     * 
     * @note 只有在编译时启用了SPDLOG_ENABLE_MULTIPROCESS才有效
     */
    static void enable() {
#ifdef SPDLOG_ENABLE_MULTIPROCESS
        enabled_.store(true, std::memory_order_release);
#endif
    }
    
    /**
     * @brief 禁用多进程模式
     * 
     * @details
     * 禁用多进程模式后：
     * - SharedMemoryProducerSink将使用回退sink（如果配置了）
     * - 或者直接丢弃消息（如果没有配置回退sink）
     * - 原始spdlog行为完全不受影响
     */
    static void disable() {
#ifdef SPDLOG_ENABLE_MULTIPROCESS
        enabled_.store(false, std::memory_order_release);
#endif
    }
    
    /**
     * @brief 设置多进程模式状态
     * @param enabled true表示启用，false表示禁用
     */
    static void set_enabled(bool enabled) {
#ifdef SPDLOG_ENABLE_MULTIPROCESS
        enabled_.store(enabled, std::memory_order_release);
#else
        (void)enabled;  // 避免未使用参数警告
#endif
    }

private:
#ifdef SPDLOG_ENABLE_MULTIPROCESS
    // 多进程模式状态（默认启用）
    static std::atomic<bool> enabled_;
#endif
};

// 便捷函数

/**
 * @brief 检查多进程模式是否启用
 * @return 如果多进程模式启用返回true，否则返回false
 */
inline bool is_enabled() {
    return MultiprocessMode::is_enabled();
}

/**
 * @brief 启用多进程模式
 */
inline void enable() {
    MultiprocessMode::enable();
}

/**
 * @brief 禁用多进程模式
 */
inline void disable() {
    MultiprocessMode::disable();
}

/**
 * @brief 设置多进程模式状态
 * @param enabled true表示启用，false表示禁用
 */
inline void set_enabled(bool enabled) {
    MultiprocessMode::set_enabled(enabled);
}

} // namespace multiprocess
} // namespace spdlog
