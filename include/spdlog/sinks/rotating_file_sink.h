// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file rotating_file_sink.h
 * @brief 基于文件大小的轮转文件 sink
 * 
 * @details
 * 提供了自动轮转日志文件的功能，当文件达到指定大小时自动创建新文件。
 * 这是生产环境中最常用的文件 sink 之一。
 * 
 * **核心特性**：
 * - 基于文件大小自动轮转
 * - 保留指定数量的历史文件
 * - 自动删除最旧的文件
 * - 支持启动时轮转
 * - 支持多线程和单线程版本
 * 
 * **轮转机制**：
 * 当当前日志文件达到最大大小时：
 * 1. 关闭当前文件
 * 2. 重命名现有文件（log.txt -> log.1.txt, log.1.txt -> log.2.txt, ...）
 * 3. 删除最旧的文件（如果超过最大文件数）
 * 4. 创建新的日志文件
 * 
 * **文件命名规则**：
 * - 当前文件: base_filename.txt
 * - 历史文件: base_filename.1.txt, base_filename.2.txt, ...
 * - 数字越大，文件越旧
 * 
 * **使用场景**：
 * - 长期运行的应用程序
 * - 需要控制日志文件大小的场景
 * - 需要保留一定数量历史日志的场景
 * 
 * @note 如果需要按日期分割文件，请使用 daily_file_sink
 * @see daily_file_sink 按日期分割的文件 sink
 * 
 * @par 使用示例
 * @code
 * // 创建轮转文件 logger
 * // 每个文件最大 5MB，保留 3 个文件
 * auto logger = spdlog::rotating_logger_mt("rotating_logger", 
 *                                          "logs/rotating.log", 
 *                                          1024 * 1024 * 5,  // 5MB
 *                                          3);                // 保留 3 个文件
 * 
 * // 文件轮转顺序：
 * // rotating.log (当前) -> rotating.1.log -> rotating.2.log -> rotating.3.log (删除)
 * 
 * logger->info("这条消息会写入当前文件");
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
 * @class rotating_file_sink
 * @brief 基于文件大小的轮转文件 sink 类
 * 
 * @details
 * 这是一个自动轮转的文件 sink，当文件大小达到限制时自动创建新文件。
 * 继承自 base_sink，自动处理线程同步和格式化。
 * 
 * **核心功能**：
 * - 监控当前文件大小
 * - 自动轮转文件
 * - 管理历史文件
 * - 删除过期文件
 * 
 * **轮转策略**：
 * - 每次写入后检查文件大小
 * - 达到最大大小时触发轮转
 * - 保留指定数量的历史文件
 * - 自动删除超出数量的旧文件
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
class rotating_file_sink final : public base_sink<Mutex> {
public:
    /**
     * @brief 最大文件数量限制
     * 
     * @details
     * 为了防止创建过多文件，设置了最大文件数量限制。
     * 如果尝试设置超过此值的文件数量，会抛出异常。
     */
    static constexpr size_t MaxFiles = 200000;
    
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个轮转文件 sink，配置文件大小和数量限制。
     * 
     * **参数说明**：
     * - base_filename: 基础文件名，如 "logs/app.log"
     * - max_size: 单个文件的最大字节数
     * - max_files: 保留的最大文件数量（包括当前文件）
     * - rotate_on_open: 是否在打开时立即轮转
     * 
     * **轮转行为**：
     * - rotate_on_open = false（默认）: 追加到现有文件
     * - rotate_on_open = true: 启动时立即轮转，创建新文件
     * 
     * @param base_filename 基础文件名（不包括轮转索引）
     * @param max_size 单个文件的最大字节数
     * @param max_files 保留的最大文件数量
     * @param rotate_on_open 是否在打开时立即轮转
     * @param event_handlers 文件事件处理器（可选）
     * 
     * @throws spdlog_ex 如果 max_files 超过 MaxFiles 或无法创建文件
     * 
     * @par 使用示例
     * @code
     * // 基本使用：5MB 文件，保留 3 个
     * auto sink1 = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3);
     * 
     * // 启动时轮转
     * auto sink2 = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3, true);
     * 
     * // 使用文件事件处理器
     * spdlog::file_event_handlers handlers;
     * handlers.after_open = [](spdlog::filename_t filename, std::FILE* file) {
     *     std::cout << "文件已打开: " << filename << std::endl;
     * };
     * auto sink3 = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3, false, handlers);
     * @endcode
     */
    rotating_file_sink(filename_t base_filename,
                       std::size_t max_size,
                       std::size_t max_files,
                       bool rotate_on_open = false,
                       const file_event_handlers &event_handlers = {});
    
    /**
     * @brief 计算轮转文件名
     * 
     * @details
     * 根据基础文件名和索引计算轮转后的文件名。
     * 
     * **命名规则**：
     * - index = 0: base_filename (当前文件)
     * - index > 0: base_filename.index (历史文件)
     * 
     * @param filename 基础文件名
     * @param index 轮转索引（0 表示当前文件）
     * @return 计算出的文件名
     * 
     * @par 使用示例
     * @code
     * auto name0 = rotating_file_sink_mt::calc_filename("logs/app.log", 0);
     * // 返回: "logs/app.log"
     * 
     * auto name1 = rotating_file_sink_mt::calc_filename("logs/app.log", 1);
     * // 返回: "logs/app.1.log"
     * 
     * auto name2 = rotating_file_sink_mt::calc_filename("logs/app.log", 2);
     * // 返回: "logs/app.2.log"
     * @endcode
     */
    static filename_t calc_filename(const filename_t &filename, std::size_t index);
    
    /**
     * @brief 获取当前文件名
     * 
     * @details
     * 返回当前正在写入的日志文件名。
     * 
     * @return 当前文件名
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3);
     * std::cout << "当前文件: " << sink->filename() << std::endl;
     * @endcode
     */
    filename_t filename();
    
    /**
     * @brief 立即执行文件轮转
     * 
     * @details
     * 手动触发文件轮转，无论当前文件大小是否达到限制。
     * 
     * **使用场景**：
     * - 在特定事件后强制创建新文件
     * - 在应用程序重启前保存当前日志
     * - 在关键操作前后分隔日志
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3);
     * 
     * // 记录一些日志
     * // ...
     * 
     * // 手动轮转，开始新文件
     * sink->rotate_now();
     * @endcode
     */
    void rotate_now();
    
    /**
     * @brief 设置最大文件大小
     * 
     * @details
     * 动态修改单个文件的最大字节数。
     * 新的限制会在下次写入时生效。
     * 
     * @param max_size 新的最大文件大小（字节）
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3);
     * 
     * // 运行时修改最大文件大小为 10MB
     * sink->set_max_size(1024 * 1024 * 10);
     * @endcode
     */
    void set_max_size(std::size_t max_size);
    
    /**
     * @brief 获取最大文件大小
     * 
     * @details
     * 返回当前配置的最大文件大小。
     * 
     * @return 最大文件大小（字节）
     */
    std::size_t get_max_size();
    
    /**
     * @brief 设置最大文件数量
     * 
     * @details
     * 动态修改保留的最大文件数量。
     * 如果新值小于当前值，会在下次轮转时删除多余的旧文件。
     * 
     * @param max_files 新的最大文件数量
     * 
     * @throws spdlog_ex 如果 max_files 超过 MaxFiles
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
     *     "logs/app.log", 1024 * 1024 * 5, 3);
     * 
     * // 运行时修改为保留 5 个文件
     * sink->set_max_files(5);
     * @endcode
     */
    void set_max_files(std::size_t max_files);
    
    /**
     * @brief 获取最大文件数量
     * 
     * @details
     * 返回当前配置的最大文件数量。
     * 
     * @return 最大文件数量
     */
    std::size_t get_max_files();

protected:
    /**
     * @brief 实际的日志写入操作
     * 
     * @details
     * 将格式化后的日志消息写入文件，并检查是否需要轮转。
     * 
     * **实现细节**：
     * - 使用 formatter_ 格式化消息
     * - 写入当前文件
     * - 更新当前文件大小
     * - 检查是否需要轮转
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
     * 
     * @note 这是一个 protected 方法，由 base_sink 调用
     */
    void flush_() override;

private:
    /**
     * @brief 执行文件轮转
     * 
     * @details
     * 执行实际的文件轮转操作：
     * 1. 关闭当前文件
     * 2. 重命名所有现有文件（log.txt -> log.1.txt, log.1.txt -> log.2.txt, ...）
     * 3. 删除超出数量限制的最旧文件
     * 4. 创建新的当前文件
     * 
     * **轮转顺序**：
     * - log.txt -> log.1.txt
     * - log.1.txt -> log.2.txt
     * - log.2.txt -> log.3.txt
     * - log.3.txt -> 删除（如果超过 max_files）
     * 
     * @note 这是一个私有方法，由 sink_it_() 或 rotate_now() 调用
     */
    void rotate_();

    /**
     * @brief 重命名文件
     * 
     * @details
     * 删除目标文件（如果存在），然后将源文件重命名为目标文件。
     * 
     * @param src_filename 源文件名
     * @param target_filename 目标文件名
     * @return 成功返回 true，失败返回 false
     * 
     * @note 这是一个私有方法，由 rotate_() 调用
     */
    bool rename_file_(const filename_t &src_filename, const filename_t &target_filename);

    filename_t base_filename_;          ///< 基础文件名
    std::size_t max_size_;              ///< 单个文件的最大字节数
    std::size_t max_files_;             ///< 保留的最大文件数量
    std::size_t current_size_;          ///< 当前文件的大小
    details::file_helper file_helper_;  ///< 文件助手对象
};

/**
 * @typedef rotating_file_sink_mt
 * @brief 多线程安全的轮转文件 sink
 * 
 * @details
 * 使用 std::mutex 保护，可以安全地在多个线程中使用。
 * 这是最常用的轮转文件 sink 类型。
 */
using rotating_file_sink_mt = rotating_file_sink<std::mutex>;

/**
 * @typedef rotating_file_sink_st
 * @brief 单线程的轮转文件 sink
 * 
 * @details
 * 不使用互斥锁，性能更好，但只能在单线程环境使用。
 */
using rotating_file_sink_st = rotating_file_sink<details::null_mutex>;

}  // namespace sinks

/**
 * @brief 创建多线程安全的轮转文件 logger
 * 
 * @details
 * 这是一个工厂函数，创建一个自动轮转的文件 logger。
 * 
 * **特点**：
 * - 线程安全
 * - 基于文件大小自动轮转
 * - 保留指定数量的历史文件
 * - 自动删除最旧的文件
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 基础文件名
 * @param max_file_size 单个文件的最大字节数
 * @param max_files 保留的最大文件数量
 * @param rotate_on_open 是否在打开时立即轮转（默认为 false）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果 max_files 超过限制或无法创建文件
 * 
 * @par 使用示例
 * @code
 * // 创建轮转文件 logger：5MB 文件，保留 3 个
 * auto logger = spdlog::rotating_logger_mt("rotating_logger", 
 *                                          "logs/rotating.log",
 *                                          1024 * 1024 * 5,  // 5MB
 *                                          3);                // 3 个文件
 * 
 * logger->info("日志消息");
 * 
 * // 启动时轮转
 * auto logger2 = spdlog::rotating_logger_mt("rotating_logger2",
 *                                           "logs/rotating.log",
 *                                           1024 * 1024 * 5,
 *                                           3,
 *                                           true);  // 启动时轮转
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> rotating_logger_mt(const std::string &logger_name,
                                           const filename_t &filename,
                                           size_t max_file_size,
                                           size_t max_files,
                                           bool rotate_on_open = false,
                                           const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::rotating_file_sink_mt>(
        logger_name, filename, max_file_size, max_files, rotate_on_open, event_handlers);
}

/**
 * @brief 创建单线程的轮转文件 logger
 * 
 * @details
 * 创建一个自动轮转的单线程文件 logger。
 * 性能更好，但不是线程安全的。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 基础文件名
 * @param max_file_size 单个文件的最大字节数
 * @param max_files 保留的最大文件数量
 * @param rotate_on_open 是否在打开时立即轮转（默认为 false）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果 max_files 超过限制或无法创建文件
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto logger = spdlog::rotating_logger_st("rotating_logger",
 *                                          "logs/rotating.log",
 *                                          1024 * 1024 * 5,
 *                                          3);
 * logger->info("单线程轮转日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> rotating_logger_st(const std::string &logger_name,
                                           const filename_t &filename,
                                           size_t max_file_size,
                                           size_t max_files,
                                           bool rotate_on_open = false,
                                           const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::rotating_file_sink_st>(
        logger_name, filename, max_file_size, max_files, rotate_on_open, event_handlers);
}
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "rotating_file_sink-inl.h"
#endif

