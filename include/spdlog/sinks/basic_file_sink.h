// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file basic_file_sink.h
 * @brief 基础文件 sink 实现
 * 
 * @details
 * 提供了最简单的文件日志输出功能，将日志写入单个文件。
 * 这是最基础的文件 sink，适合简单的日志记录需求。
 * 
 * **核心特性**：
 * - 输出到单个文件
 * - 支持追加或截断模式
 * - 支持多线程和单线程版本
 * - 支持文件事件处理器（打开、关闭等）
 * - 自动创建目录（如果不存在）
 * 
 * **使用场景**：
 * - 简单的日志文件记录
 * - 不需要文件轮转的场景
 * - 日志文件大小可控的应用
 * 
 * **限制**：
 * - 不支持文件轮转（文件会一直增长）
 * - 不支持按时间分割文件
 * - 如需这些功能，请使用 rotating_file_sink 或 daily_file_sink
 * 
 * @note 如果需要文件轮转功能，请使用 rotating_file_sink
 * @note 如果需要按日期分割文件，请使用 daily_file_sink
 * 
 * @par 使用示例
 * @code
 * // 创建基础文件 logger（追加模式）
 * auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/app.log");
 * file_logger->info("日志消息");
 * 
 * // 创建基础文件 logger（截断模式，清空现有文件）
 * auto file_logger2 = spdlog::basic_logger_mt("file_logger2", "logs/app.log", true);
 * 
 * // 手动创建 sink
 * auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log");
 * auto logger = std::make_shared<spdlog::logger>("my_logger", file_sink);
 * @endcode
 */

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/sinks/base_sink.h>

#include <mutex>
#include <string>

namespace spdlog {
namespace sinks {

/**
 * @class basic_file_sink
 * @brief 基础文件 sink 类
 * 
 * @details
 * 这是一个简单的文件 sink 实现，将日志写入单个文件。
 * 继承自 base_sink，自动处理线程同步和格式化。
 * 
 * **核心功能**：
 * - 打开和管理文件
 * - 写入格式化后的日志消息
 * - 刷新文件缓冲区
 * - 支持文件截断
 * 
 * **文件操作**：
 * - 自动创建不存在的目录
 * - 支持追加模式（默认）和截断模式
 * - 使用 file_helper 管理文件操作
 * 
 * **线程安全性**：
 * - 通过模板参数 Mutex 控制
 * - _mt 版本：线程安全
 * - _st 版本：单线程，性能更好
 * 
 * @tparam Mutex 互斥锁类型
 * 
 * @note 这是一个 final 类，不能被继承
 */
template <typename Mutex>
class basic_file_sink final : public base_sink<Mutex> {
public:
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个文件 sink，打开指定的文件用于日志输出。
     * 
     * **文件模式**：
     * - truncate = false（默认）: 追加模式，保留现有内容
     * - truncate = true: 截断模式，清空现有内容
     * 
     * **目录处理**：
     * - 如果文件路径中的目录不存在，会自动创建
     * - 如果无法创建目录或打开文件，会抛出 spdlog_ex 异常
     * 
     * @param filename 日志文件路径（支持相对路径和绝对路径）
     * @param truncate 是否截断文件（true=清空，false=追加）
     * @param event_handlers 文件事件处理器（可选）
     * 
     * @throws spdlog_ex 如果无法打开文件或创建目录
     * 
     * @par 使用示例
     * @code
     * // 追加模式（默认）
     * auto sink1 = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log");
     * 
     * // 截断模式（清空现有文件）
     * auto sink2 = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log", true);
     * 
     * // 使用文件事件处理器
     * spdlog::file_event_handlers handlers;
     * handlers.after_open = [](spdlog::filename_t filename, std::FILE* file) {
     *     std::cout << "文件已打开: " << filename << std::endl;
     * };
     * auto sink3 = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
     *     "logs/app.log", false, handlers);
     * @endcode
     */
    explicit basic_file_sink(const filename_t &filename,
                             bool truncate = false,
                             const file_event_handlers &event_handlers = {});
    
    /**
     * @brief 获取文件名
     * 
     * @details
     * 返回此 sink 正在写入的文件路径。
     * 
     * @return 文件路径的常量引用
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log");
     * std::cout << "日志文件: " << sink->filename() << std::endl;
     * @endcode
     */
    const filename_t &filename() const;
    
    /**
     * @brief 截断文件
     * 
     * @details
     * 清空文件内容，将文件大小重置为 0。
     * 这个操作会：
     * 1. 关闭当前文件
     * 2. 以截断模式重新打开文件
     * 3. 清空所有现有内容
     * 
     * @warning 这个操作会永久删除文件中的所有内容，请谨慎使用
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log");
     * 
     * // 写入一些日志
     * // ...
     * 
     * // 清空文件，重新开始
     * sink->truncate();
     * @endcode
     */
    void truncate();

protected:
    /**
     * @brief 实际的日志写入操作
     * 
     * @details
     * 将格式化后的日志消息写入文件。
     * 这个方法由 base_sink 在锁保护下调用。
     * 
     * **实现细节**：
     * - 使用 formatter_ 格式化消息
     * - 使用 file_helper_ 写入文件
     * - 不需要手动加锁（已在 base_sink 中处理）
     * 
     * @param msg 要写入的日志消息
     * 
     * @note 这是一个 protected 方法，由 base_sink 调用
     */
    void sink_it_(const details::log_msg &msg) override;
    
    /**
     * @brief 实际的刷新操作
     * 
     * @details
     * 将文件缓冲区中的所有内容立即写入磁盘。
     * 这个方法由 base_sink 在锁保护下调用。
     * 
     * **实现细节**：
     * - 调用 file_helper_ 的 flush 方法
     * - 确保所有缓冲的数据都被写入磁盘
     * 
     * @note 这是一个 protected 方法，由 base_sink 调用
     */
    void flush_() override;

private:
    /**
     * @brief 文件助手对象
     * 
     * @details
     * 封装了文件操作的细节，包括：
     * - 打开和关闭文件
     * - 写入数据
     * - 刷新缓冲区
     * - 处理文件事件
     */
    details::file_helper file_helper_;
};

/**
 * @typedef basic_file_sink_mt
 * @brief 多线程安全的基础文件 sink
 * 
 * @details
 * 使用 std::mutex 保护，可以安全地在多个线程中使用。
 * 这是最常用的文件 sink 类型。
 */
using basic_file_sink_mt = basic_file_sink<std::mutex>;

/**
 * @typedef basic_file_sink_st
 * @brief 单线程的基础文件 sink
 * 
 * @details
 * 不使用互斥锁，性能更好，但只能在单线程环境使用。
 */
using basic_file_sink_st = basic_file_sink<details::null_mutex>;

}  // namespace sinks

/**
 * @brief 创建多线程安全的基础文件 logger
 * 
 * @details
 * 这是一个工厂函数，创建一个输出到文件的 logger。
 * 
 * **特点**：
 * - 线程安全
 * - 输出到单个文件
 * - 支持追加或截断模式
 * - 自动创建目录
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 日志文件路径
 * @param truncate 是否截断文件（默认为 false，追加模式）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果无法打开文件或创建目录
 * 
 * @par 使用示例
 * @code
 * // 追加模式（默认）
 * auto logger = spdlog::basic_logger_mt("file_logger", "logs/app.log");
 * logger->info("日志消息");
 * 
 * // 截断模式（清空现有文件）
 * auto logger2 = spdlog::basic_logger_mt("file_logger2", "logs/app.log", true);
 * 
 * // 使用文件事件处理器
 * spdlog::file_event_handlers handlers;
 * handlers.before_open = [](spdlog::filename_t filename) {
 *     std::cout << "即将打开文件: " << filename << std::endl;
 * };
 * auto logger3 = spdlog::basic_logger_mt("file_logger3", "logs/app.log", 
 *                                        false, handlers);
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> basic_logger_mt(const std::string &logger_name,
                                               const filename_t &filename,
                                               bool truncate = false,
                                               const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::basic_file_sink_mt>(logger_name, filename, truncate,
                                                               event_handlers);
}

/**
 * @brief 创建单线程的基础文件 logger
 * 
 * @details
 * 创建一个输出到文件的单线程 logger。
 * 性能更好，但不是线程安全的。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 日志文件路径
 * @param truncate 是否截断文件（默认为 false，追加模式）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果无法打开文件或创建目录
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto logger = spdlog::basic_logger_st("file_logger", "logs/app.log");
 * logger->info("单线程文件日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> basic_logger_st(const std::string &logger_name,
                                               const filename_t &filename,
                                               bool truncate = false,
                                               const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::basic_file_sink_st>(logger_name, filename, truncate,
                                                               event_handlers);
}

}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "basic_file_sink-inl.h"
#endif

