// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file stdout_color_sinks.h
 * @brief 支持颜色输出的控制台 sink
 * 
 * @details
 * 提供了支持彩色输出的控制台 sink 实现。
 * 根据不同的操作系统使用不同的颜色实现：
 * - Windows: 使用 Windows Console API
 * - Unix-like: 使用 ANSI 转义序列
 * 
 * **核心特性**：
 * - 根据日志级别自动着色
 * - 跨平台支持（Windows 和 Unix-like 系统）
 * - 支持自动检测终端颜色支持
 * - 支持多线程和单线程版本
 * 
 * **颜色方案**：
 * - trace: 白色
 * - debug: 青色
 * - info: 绿色
 * - warn: 黄色
 * - error: 红色
 * - critical: 红色加粗/高亮
 * 
 * **颜色模式**：
 * - automatic: 自动检测终端是否支持颜色
 * - always: 总是使用颜色
 * - never: 从不使用颜色
 * 
 * @note 这是最常用的控制台输出 sink，推荐用于开发和调试
 * @see stdout_sinks.h 不支持颜色的控制台输出
 * 
 * @par 使用示例
 * @code
 * // 创建彩色控制台 logger（最常用）
 * auto console = spdlog::stdout_color_mt("console");
 * console->trace("Trace message");    // 白色
 * console->debug("Debug message");    // 青色
 * console->info("Info message");      // 绿色
 * console->warn("Warning message");   // 黄色
 * console->error("Error message");    // 红色
 * console->critical("Critical!");     // 红色加粗
 * 
 * // 强制启用颜色
 * auto console2 = spdlog::stdout_color_mt("console2", spdlog::color_mode::always);
 * 
 * // 禁用颜色
 * auto console3 = spdlog::stdout_color_mt("console3", spdlog::color_mode::never);
 * @endcode
 */

#ifdef _WIN32
#include <spdlog/sinks/wincolor_sink.h>
#else
#include <spdlog/sinks/ansicolor_sink.h>
#endif

#include <spdlog/details/synchronous_factory.h>

namespace spdlog {
namespace sinks {

#ifdef _WIN32
/**
 * @typedef stdout_color_sink_mt
 * @brief 多线程安全的彩色标准输出 sink（Windows 版本）
 * 
 * @details
 * 在 Windows 平台上，使用 Windows Console API 实现颜色输出。
 * 线程安全，可以在多线程环境中使用。
 */
using stdout_color_sink_mt = wincolor_stdout_sink_mt;

/**
 * @typedef stdout_color_sink_st
 * @brief 单线程的彩色标准输出 sink（Windows 版本）
 * 
 * @details
 * 在 Windows 平台上，使用 Windows Console API 实现颜色输出。
 * 不是线程安全的，但性能更好。
 */
using stdout_color_sink_st = wincolor_stdout_sink_st;

/**
 * @typedef stderr_color_sink_mt
 * @brief 多线程安全的彩色标准错误输出 sink（Windows 版本）
 */
using stderr_color_sink_mt = wincolor_stderr_sink_mt;

/**
 * @typedef stderr_color_sink_st
 * @brief 单线程的彩色标准错误输出 sink（Windows 版本）
 */
using stderr_color_sink_st = wincolor_stderr_sink_st;

#else

/**
 * @typedef stdout_color_sink_mt
 * @brief 多线程安全的彩色标准输出 sink（Unix-like 版本）
 * 
 * @details
 * 在 Unix-like 平台上，使用 ANSI 转义序列实现颜色输出。
 * 线程安全，可以在多线程环境中使用。
 */
using stdout_color_sink_mt = ansicolor_stdout_sink_mt;

/**
 * @typedef stdout_color_sink_st
 * @brief 单线程的彩色标准输出 sink（Unix-like 版本）
 * 
 * @details
 * 在 Unix-like 平台上，使用 ANSI 转义序列实现颜色输出。
 * 不是线程安全的，但性能更好。
 */
using stdout_color_sink_st = ansicolor_stdout_sink_st;

/**
 * @typedef stderr_color_sink_mt
 * @brief 多线程安全的彩色标准错误输出 sink（Unix-like 版本）
 */
using stderr_color_sink_mt = ansicolor_stderr_sink_mt;

/**
 * @typedef stderr_color_sink_st
 * @brief 单线程的彩色标准错误输出 sink（Unix-like 版本）
 */
using stderr_color_sink_st = ansicolor_stderr_sink_st;

#endif

}  // namespace sinks

/**
 * @brief 创建多线程安全的彩色 stdout logger
 * 
 * @details
 * 这是最常用的 logger 创建函数，创建一个支持彩色输出的控制台 logger。
 * 
 * **特点**：
 * - 线程安全
 * - 输出到 stdout
 * - 根据日志级别自动着色
 * - 支持颜色模式配置
 * 
 * **颜色模式**：
 * - automatic（默认）: 自动检测终端是否支持颜色
 * - always: 总是使用颜色（即使输出被重定向）
 * - never: 从不使用颜色
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param mode 颜色模式，默认为自动检测
 * @return logger 的共享指针
 * 
 * @par 使用示例
 * @code
 * // 使用默认颜色模式（自动检测）
 * auto console = spdlog::stdout_color_mt("console");
 * console->info("彩色信息");
 * 
 * // 强制启用颜色
 * auto console_color = spdlog::stdout_color_mt("console_color", 
 *                                              spdlog::color_mode::always);
 * 
 * // 禁用颜色
 * auto console_plain = spdlog::stdout_color_mt("console_plain", 
 *                                              spdlog::color_mode::never);
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stdout_color_mt(const std::string &logger_name,
                                        color_mode mode = color_mode::automatic);

/**
 * @brief 创建单线程的彩色 stdout logger
 * 
 * @details
 * 创建一个支持彩色输出的单线程控制台 logger。
 * 性能更好，但不是线程安全的。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param mode 颜色模式，默认为自动检测
 * @return logger 的共享指针
 * 
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto console = spdlog::stdout_color_st("console");
 * console->info("单线程彩色日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stdout_color_st(const std::string &logger_name,
                                        color_mode mode = color_mode::automatic);

/**
 * @brief 创建多线程安全的彩色 stderr logger
 * 
 * @details
 * 创建一个输出到标准错误的彩色 logger。
 * 适合用于错误和警告信息。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param mode 颜色模式，默认为自动检测
 * @return logger 的共享指针
 * 
 * @par 使用示例
 * @code
 * auto error_logger = spdlog::stderr_color_mt("errors");
 * error_logger->error("彩色错误信息");
 * error_logger->critical("彩色严重错误");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stderr_color_mt(const std::string &logger_name,
                                        color_mode mode = color_mode::automatic);

/**
 * @brief 创建单线程的彩色 stderr logger
 * 
 * @details
 * 创建一个输出到标准错误的单线程彩色 logger。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param mode 颜色模式，默认为自动检测
 * @return logger 的共享指针
 * 
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto error_logger = spdlog::stderr_color_st("errors");
 * error_logger->error("单线程彩色错误日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stderr_color_st(const std::string &logger_name,
                                        color_mode mode = color_mode::automatic);

}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "stdout_color_sinks-inl.h"
#endif

