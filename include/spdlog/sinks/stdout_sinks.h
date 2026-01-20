// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file stdout_sinks.h
 * @brief 标准输出和标准错误输出的 sink 实现
 * 
 * @details
 * 提供了将日志输出到控制台的 sink 实现。
 * 包括标准输出（stdout）和标准错误（stderr）两种类型。
 * 
 * **核心特性**：
 * - 输出到标准输出流（stdout）或标准错误流（stderr）
 * - 支持多线程和单线程版本
 * - 跨平台支持（Windows 和 Unix-like 系统）
 * - 使用全局互斥锁避免多个 logger 输出混乱
 * 
 * **线程安全性**：
 * - _mt 后缀：多线程安全版本，使用互斥锁
 * - _st 后缀：单线程版本，无锁开销
 * 
 * **使用场景**：
 * - stdout: 用于一般信息输出
 * - stderr: 用于错误和警告信息输出
 * 
 * @note 这些 sink 不支持颜色输出，如需颜色请使用 stdout_color_sinks.h
 * @see stdout_color_sinks.h 支持颜色的控制台输出
 * 
 * @par 使用示例
 * @code
 * // 创建输出到 stdout 的 logger
 * auto console = spdlog::stdout_logger_mt("console");
 * console->info("Hello from stdout");
 * 
 * // 创建输出到 stderr 的 logger
 * auto error_logger = spdlog::stderr_logger_mt("errors");
 * error_logger->error("Error message to stderr");
 * 
 * // 手动创建 sink
 * auto stdout_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
 * auto logger = std::make_shared<spdlog::logger>("my_logger", stdout_sink);
 * @endcode
 */

#include <cstdio>
#include <spdlog/details/console_globals.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/sinks/sink.h>

#ifdef _WIN32
#include <spdlog/details/windows_include.h>
#endif

namespace spdlog {

namespace sinks {

/**
 * @class stdout_sink_base
 * @brief 标准输出 sink 的基类模板
 * 
 * @details
 * 这是 stdout_sink 和 stderr_sink 的共同基类。
 * 提供了向标准流（stdout 或 stderr）输出日志的基础功能。
 * 
 * **设计特点**：
 * - 使用全局控制台互斥锁，避免多个 logger 输出混乱
 * - 支持 Windows 和 Unix-like 系统
 * - 直接使用 FILE* 进行输出，性能较好
 * 
 * **模板参数**：
 * - ConsoleMutex: 控制台互斥锁类型
 *   - console_mutex: 真实的互斥锁（多线程版本）
 *   - console_nullmutex: 空锁（单线程版本）
 * 
 * @tparam ConsoleMutex 控制台互斥锁类型
 * 
 * @note 通常不直接使用这个类，而是使用 stdout_sink 或 stderr_sink
 */
template <typename ConsoleMutex>
class stdout_sink_base : public sink {
public:
    /**
     * @typedef mutex_t
     * @brief 互斥锁类型别名
     */
    using mutex_t = typename ConsoleMutex::mutex_t;
    
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个输出到指定文件流的 sink。
     * 
     * @param file 文件流指针（stdout 或 stderr）
     * 
     * @note 通常不直接调用，而是通过 stdout_sink 或 stderr_sink 构造
     */
    explicit stdout_sink_base(FILE *file);
    
    /**
     * @brief 析构函数
     * 
     * @details
     * 使用默认实现。不会关闭文件流（stdout/stderr 不应该被关闭）。
     */
    ~stdout_sink_base() override = default;

    /**
     * @brief 禁用拷贝构造函数
     */
    stdout_sink_base(const stdout_sink_base &other) = delete;
    
    /**
     * @brief 禁用移动构造函数
     */
    stdout_sink_base(stdout_sink_base &&other) = delete;

    /**
     * @brief 禁用拷贝赋值运算符
     */
    stdout_sink_base &operator=(const stdout_sink_base &other) = delete;
    
    /**
     * @brief 禁用移动赋值运算符
     */
    stdout_sink_base &operator=(stdout_sink_base &&other) = delete;

    /**
     * @brief 记录日志消息
     * 
     * @details
     * 将格式化后的日志消息输出到标准流。
     * 
     * **线程安全性**：
     * - 使用全局控制台互斥锁保护
     * - 避免多个 logger 的输出混乱
     * 
     * @param msg 要记录的日志消息
     */
    void log(const details::log_msg &msg) override;
    
    /**
     * @brief 刷新输出缓冲区
     * 
     * @details
     * 强制将缓冲区中的所有内容立即输出到控制台。
     * 
     * @note 调用 fflush() 来刷新文件流
     */
    void flush() override;
    
    /**
     * @brief 设置格式化模式
     * 
     * @param pattern 模式字符串
     */
    void set_pattern(const std::string &pattern) override;

    /**
     * @brief 设置格式化器
     * 
     * @param sink_formatter 格式化器的唯一指针
     */
    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;

protected:
    mutex_t &mutex_;                                ///< 全局控制台互斥锁的引用
    FILE *file_;                                    ///< 文件流指针（stdout 或 stderr）
    std::unique_ptr<spdlog::formatter> formatter_;  ///< 格式化器
#ifdef _WIN32
    HANDLE handle_;  ///< Windows 控制台句柄（用于 Windows 特定操作）
#endif  // WIN32
};

/**
 * @class stdout_sink
 * @brief 标准输出 sink
 * 
 * @details
 * 将日志输出到标准输出流（stdout）。
 * 这是最常用的控制台输出 sink。
 * 
 * **特点**：
 * - 输出到 stdout
 * - 适合一般信息输出
 * - 可以被重定向到文件或管道
 * 
 * @tparam ConsoleMutex 控制台互斥锁类型
 * 
 * @par 使用示例
 * @code
 * // 多线程版本
 * auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
 * auto logger = std::make_shared<spdlog::logger>("console", sink);
 * logger->info("输出到 stdout");
 * 
 * // 单线程版本（更快，但不是线程安全的）
 * auto sink_st = std::make_shared<spdlog::sinks::stdout_sink_st>();
 * @endcode
 */
template <typename ConsoleMutex>
class stdout_sink : public stdout_sink_base<ConsoleMutex> {
public:
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个输出到 stdout 的 sink。
     */
    stdout_sink();
};

/**
 * @class stderr_sink
 * @brief 标准错误输出 sink
 * 
 * @details
 * 将日志输出到标准错误流（stderr）。
 * 通常用于错误和警告信息。
 * 
 * **特点**：
 * - 输出到 stderr
 * - 适合错误和警告信息
 * - 可以与 stdout 分开重定向
 * - 通常不会被缓冲（立即输出）
 * 
 * @tparam ConsoleMutex 控制台互斥锁类型
 * 
 * @par 使用示例
 * @code
 * // 多线程版本
 * auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
 * auto logger = std::make_shared<spdlog::logger>("errors", sink);
 * logger->error("输出到 stderr");
 * 
 * // 单线程版本
 * auto sink_st = std::make_shared<spdlog::sinks::stderr_sink_st>();
 * @endcode
 */
template <typename ConsoleMutex>
class stderr_sink : public stdout_sink_base<ConsoleMutex> {
public:
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个输出到 stderr 的 sink。
     */
    stderr_sink();
};

/**
 * @typedef stdout_sink_mt
 * @brief 多线程安全的标准输出 sink
 * 
 * @details
 * 使用互斥锁保护，可以安全地在多个线程中使用。
 * 这是最常用的控制台输出 sink 类型。
 */
using stdout_sink_mt = stdout_sink<details::console_mutex>;

/**
 * @typedef stdout_sink_st
 * @brief 单线程的标准输出 sink
 * 
 * @details
 * 不使用互斥锁，性能更好，但只能在单线程环境使用。
 */
using stdout_sink_st = stdout_sink<details::console_nullmutex>;

/**
 * @typedef stderr_sink_mt
 * @brief 多线程安全的标准错误输出 sink
 * 
 * @details
 * 使用互斥锁保护，可以安全地在多个线程中使用。
 */
using stderr_sink_mt = stderr_sink<details::console_mutex>;

/**
 * @typedef stderr_sink_st
 * @brief 单线程的标准错误输出 sink
 * 
 * @details
 * 不使用互斥锁，性能更好，但只能在单线程环境使用。
 */
using stderr_sink_st = stderr_sink<details::console_nullmutex>;

}  // namespace sinks

/**
 * @brief 创建多线程安全的 stdout logger
 * 
 * @details
 * 这是一个工厂函数，创建一个输出到标准输出的 logger。
 * 
 * **特点**：
 * - 线程安全
 * - 输出到 stdout
 * - 使用默认格式化器
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @return logger 的共享指针
 * 
 * @par 使用示例
 * @code
 * auto console = spdlog::stdout_logger_mt("console");
 * console->info("Hello World");
 * console->warn("Warning message");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stdout_logger_mt(const std::string &logger_name);

/**
 * @brief 创建单线程的 stdout logger
 * 
 * @details
 * 创建一个输出到标准输出的单线程 logger。
 * 性能更好，但不是线程安全的。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @return logger 的共享指针
 * 
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto console = spdlog::stdout_logger_st("console");
 * console->info("Single-threaded logging");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stdout_logger_st(const std::string &logger_name);

/**
 * @brief 创建多线程安全的 stderr logger
 * 
 * @details
 * 创建一个输出到标准错误的 logger。
 * 适合用于错误和警告信息。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @return logger 的共享指针
 * 
 * @par 使用示例
 * @code
 * auto error_logger = spdlog::stderr_logger_mt("errors");
 * error_logger->error("Error occurred");
 * error_logger->critical("Critical error");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stderr_logger_mt(const std::string &logger_name);

/**
 * @brief 创建单线程的 stderr logger
 * 
 * @details
 * 创建一个输出到标准错误的单线程 logger。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @return logger 的共享指针
 * 
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto error_logger = spdlog::stderr_logger_st("errors");
 * error_logger->error("Single-threaded error logging");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stderr_logger_st(const std::string &logger_name);

}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "stdout_sinks-inl.h"
#endif

