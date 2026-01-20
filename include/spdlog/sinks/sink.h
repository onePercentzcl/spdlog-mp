// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file sink.h
 * @brief Sink（日志输出目标）的抽象接口
 * 
 * @details
 * Sink 是 spdlog 中负责实际输出日志的组件。
 * 每个 sink 代表一个输出目标，如控制台、文件、网络等。
 * 
 * **核心概念**：
 * - Logger 可以有多个 sink，将日志同时输出到多个目标
 * - 每个 sink 可以有自己的日志级别过滤
 * - 每个 sink 可以有自己的格式化器
 * - Sink 负责实际的 I/O 操作
 * 
 * **常见 Sink 类型**：
 * - stdout_sink: 输出到标准输出
 * - stderr_sink: 输出到标准错误
 * - basic_file_sink: 输出到文件
 * - rotating_file_sink: 输出到轮转文件
 * - daily_file_sink: 输出到每日文件
 * - null_sink: 不输出（用于测试）
 * 
 * **线程安全性**：
 * - sink 本身不保证线程安全
 * - 使用 _mt 后缀的 sink（如 stdout_sink_mt）是线程安全的
 * - 使用 _st 后缀的 sink（如 stdout_sink_st）是单线程的
 * 
 * @note 这是一个抽象基类，不能直接实例化
 * @see base_sink 提供了 sink 的基础实现
 * 
 * @par 使用示例
 * @code
 * // 创建多个 sink 的 logger
 * auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
 * auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/app.log");
 * 
 * // 为不同的 sink 设置不同的日志级别
 * console_sink->set_level(spdlog::level::warn);  // 控制台只显示警告及以上
 * file_sink->set_level(spdlog::level::trace);    // 文件记录所有日志
 * 
 * // 创建 logger
 * spdlog::logger logger("multi_sink", {console_sink, file_sink});
 * logger.set_level(spdlog::level::trace);  // logger 级别设为最低
 * 
 * logger.trace("这条只会写入文件");
 * logger.warn("这条会同时输出到控制台和文件");
 * @endcode
 */

#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>

namespace spdlog {

namespace sinks {

/**
 * @class sink
 * @brief Sink 的抽象基类
 * 
 * @details
 * 定义了所有 sink 必须实现的接口。
 * Sink 负责将格式化后的日志消息输出到特定的目标。
 * 
 * **核心职责**：
 * - 接收日志消息并输出
 * - 刷新缓冲区
 * - 管理自己的格式化器
 * - 过滤日志级别
 * 
 * **设计模式**：
 * - 策略模式：不同的 sink 实现不同的输出策略
 * - 模板方法模式：base_sink 提供了通用的实现框架
 * 
 * @note 通常不直接继承 sink，而是继承 base_sink
 */
class SPDLOG_API sink {
public:
    /**
     * @brief 虚析构函数
     * 
     * @details
     * 确保通过基类指针删除派生类对象时能正确调用派生类的析构函数。
     */
    virtual ~sink() = default;
    
    /**
     * @brief 记录日志消息（纯虚函数）
     * 
     * @details
     * 这是 sink 的核心方法，负责实际输出日志消息。
     * 
     * **实现要求**：
     * - 使用 sink 的格式化器格式化消息
     * - 将格式化后的消息输出到目标
     * - 处理可能的 I/O 错误
     * - 考虑性能（这个方法会被频繁调用）
     * 
     * **调用时机**：
     * - 当 logger 记录日志时
     * - 只有当消息的级别满足 sink 的级别要求时才会被调用
     * 
     * @param msg 要记录的日志消息，包含所有日志信息
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @warning 实现时要注意线程安全性（如果是多线程 sink）
     * 
     * @par 实现示例
     * @code
     * void my_sink::log(const spdlog::details::log_msg &msg) {
     *     // 格式化消息
     *     spdlog::memory_buf_t formatted;
     *     formatter_->format(msg, formatted);
     *     
     *     // 输出到目标（例如文件）
     *     file_.write(formatted.data(), formatted.size());
     * }
     * @endcode
     */
    virtual void log(const details::log_msg &msg) = 0;
    
    /**
     * @brief 刷新缓冲区（纯虚函数）
     * 
     * @details
     * 强制将缓冲区中的所有日志立即写入到输出目标。
     * 
     * **使用场景**：
     * - 程序即将退出时
     * - 记录关键日志后
     * - 定期刷新（通过 flush_every）
     * - 手动调用 logger->flush()
     * 
     * **实现要求**：
     * - 确保所有缓冲的数据都被写入
     * - 调用底层 I/O 的刷新操作（如 fflush）
     * - 处理可能的 I/O 错误
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @warning 频繁刷新会影响性能，应该谨慎使用
     * 
     * @par 实现示例
     * @code
     * void my_sink::flush() {
     *     // 刷新文件缓冲区
     *     if (file_.is_open()) {
     *         file_.flush();
     *     }
     * }
     * @endcode
     */
    virtual void flush() = 0;
    
    /**
     * @brief 设置格式化模式（纯虚函数）
     * 
     * @details
     * 使用模式字符串设置 sink 的格式化器。
     * 这是一个便捷方法，内部会创建 pattern_formatter。
     * 
     * @param pattern 模式字符串（如 "[%Y-%m-%d %H:%M:%S.%e] [%l] %v"）
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @see pattern_formatter 了解模式字符串的语法
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
     * sink->set_pattern("[%H:%M:%S] %v");
     * @endcode
     */
    virtual void set_pattern(const std::string &pattern) = 0;
    
    /**
     * @brief 设置格式化器（纯虚函数）
     * 
     * @details
     * 直接设置 sink 的格式化器对象。
     * 允许使用自定义的格式化器实现。
     * 
     * @param sink_formatter 格式化器的唯一指针（所有权转移）
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @note sink 会接管格式化器的所有权
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
     * 
     * // 使用自定义格式化器
     * auto formatter = std::make_unique<my_custom_formatter>();
     * sink->set_formatter(std::move(formatter));
     * @endcode
     */
    virtual void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) = 0;

    /**
     * @brief 设置 sink 的日志级别
     * 
     * @details
     * 设置此 sink 接受的最低日志级别。
     * 只有级别大于或等于此级别的消息才会被输出。
     * 
     * **级别过滤机制**：
     * 1. Logger 级别：第一层过滤
     * 2. Sink 级别：第二层过滤（这里设置的）
     * 
     * 只有同时满足两个级别要求的消息才会被输出。
     * 
     * @param log_level 日志级别（trace, debug, info, warn, error, critical, off）
     * 
     * @par 使用示例
     * @code
     * auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
     * auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log");
     * 
     * // 控制台只显示警告及以上
     * console_sink->set_level(spdlog::level::warn);
     * 
     * // 文件记录所有日志
     * file_sink->set_level(spdlog::level::trace);
     * 
     * spdlog::logger logger("multi_sink", {console_sink, file_sink});
     * logger.set_level(spdlog::level::trace);
     * 
     * logger.debug("只会写入文件");
     * logger.error("会同时输出到控制台和文件");
     * @endcode
     */
    void set_level(level::level_enum log_level);
    
    /**
     * @brief 获取 sink 的日志级别
     * 
     * @details
     * 返回此 sink 当前设置的最低日志级别。
     * 
     * @return 当前的日志级别
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
     * sink->set_level(spdlog::level::warn);
     * 
     * if (sink->level() == spdlog::level::warn) {
     *     std::cout << "Sink 级别是 warn" << std::endl;
     * }
     * @endcode
     */
    level::level_enum level() const;
    
    /**
     * @brief 检查是否应该记录指定级别的消息
     * 
     * @details
     * 判断给定级别的消息是否满足此 sink 的级别要求。
     * 
     * **判断逻辑**：
     * - 如果 msg_level >= sink_level，返回 true
     * - 否则返回 false
     * 
     * @param msg_level 要检查的消息级别
     * @return 如果应该记录返回 true，否则返回 false
     * 
     * @note 这个方法通常由 logger 内部调用，用户很少直接使用
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
     * sink->set_level(spdlog::level::warn);
     * 
     * if (sink->should_log(spdlog::level::info)) {
     *     // 不会执行，因为 info < warn
     * }
     * 
     * if (sink->should_log(spdlog::level::error)) {
     *     // 会执行，因为 error >= warn
     * }
     * @endcode
     */
    bool should_log(level::level_enum msg_level) const;

protected:
    /**
     * @brief Sink 的日志级别
     * 
     * @details
     * 存储此 sink 的最低日志级别。
     * 默认值是 trace，表示接受所有级别的日志。
     * 
     * @note 这是一个 protected 成员，只能被派生类访问
     */
    level_t level_{level::trace};
};

}  // namespace sinks
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "sink-inl.h"
#endif

