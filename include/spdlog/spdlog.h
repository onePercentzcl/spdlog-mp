/**
 * @file spdlog.h
 * @brief spdlog 主头文件 - 包含所有公共 API
 * @author Gabi Melman 及 spdlog 贡献者
 * @copyright 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
 *            根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)
 * 
 * @details
 * 这是 spdlog 库的主入口头文件，包含了所有用户需要的公共 API。
 * 
 * spdlog 是一个快速、仅头文件的 C++ 日志库，具有以下特点：
 * - 非常快速：使用高效的格式化和缓冲机制
 * - 仅头文件：无需编译，包含头文件即可使用
 * - 功能丰富：支持多种输出目标、格式化选项和日志级别
 * - 跨平台：支持 Windows、Linux、macOS 等平台
 * - 线程安全：提供线程安全的 logger 实现
 * 
 * 基本使用示例：
 * @code
 * #include "spdlog/spdlog.h"
 * 
 * int main() {
 *     // 使用默认 logger 记录日志
 *     spdlog::info("欢迎使用 spdlog!");
 *     spdlog::error("发生了一些错误: {}", 42);
 *     
 *     // 创建自定义 logger
 *     auto console = spdlog::stdout_color_mt("console");
 *     console->info("这是一条彩色日志");
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @see example.cpp 查看更多使用示例
 */

#ifndef SPDLOG_H
#define SPDLOG_H

#pragma once

#include <spdlog/common.h>
#include <spdlog/details/registry.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/logger.h>
#include <spdlog/version.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

/**
 * @namespace spdlog
 * @brief spdlog 库的主命名空间
 * @details 包含所有公共 API、类型定义和工具函数
 */
namespace spdlog {

/**
 * @typedef default_factory
 * @brief 默认的 logger 工厂类型
 * @details 使用同步工厂创建 logger，适用于大多数场景
 */
using default_factory = synchronous_factory;

/**
 * @brief 使用模板化的 sink 类型创建并注册一个 logger
 * 
 * @tparam Sink Sink 类型（如 daily_file_sink_st, rotating_file_sink_mt 等）
 * @tparam SinkArgs Sink 构造函数参数类型
 * @param logger_name Logger 的唯一名称，用于后续通过 get() 获取
 * @param sink_args Sink 构造函数所需的参数
 * @return 指向新创建 logger 的智能指针
 * 
 * @details
 * 此函数是创建 logger 的便捷方式，它会：
 * 1. 使用提供的参数创建指定类型的 sink
 * 2. 创建一个包含该 sink 的 logger
 * 3. 根据全局设置配置 logger 的级别、格式化器和刷新级别
 * 4. 将 logger 注册到全局注册表中
 * 
 * @note 
 * - Logger 名称必须唯一，如果已存在同名 logger 会抛出异常
 * - 创建的 logger 会自动应用全局设置
 * - Sink 类型名称中的 _st 表示单线程，_mt 表示多线程
 * 
 * @throws spdlog_ex 如果 logger 名称已存在
 * 
 * @par 使用示例：
 * @code
 * // 创建每日轮转文件 logger（单线程版本）
 * auto daily_logger = spdlog::create<spdlog::sinks::daily_file_sink_st>(
 *     "daily_logger",           // logger 名称
 *     "logs/daily.txt",         // 文件路径
 *     23, 59                    // 每天 23:59 轮转
 * );
 * 
 * // 创建按大小轮转的文件 logger（多线程版本）
 * auto rotating_logger = spdlog::create<spdlog::sinks::rotating_file_sink_mt>(
 *     "rotating_logger",        // logger 名称
 *     "logs/rotating.txt",      // 文件路径
 *     1024 * 1024 * 5,          // 最大文件大小 5MB
 *     3                         // 保留 3 个备份文件
 * );
 * @endcode
 * 
 * @see initialize_logger() 用于初始化手动创建的 logger
 * @see get() 用于获取已注册的 logger
 */
template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<spdlog::logger> create(std::string logger_name, SinkArgs &&...sink_args) {
    return default_factory::create<Sink>(std::move(logger_name),
                                         std::forward<SinkArgs>(sink_args)...);
}

/**
 * @brief 初始化并注册一个手动创建的 logger
 * 
 * @param logger 要初始化的 logger 智能指针
 * 
 * @details
 * 此函数用于初始化手动创建的 logger，使其应用全局设置。
 * 它会：
 * 1. 应用全局的日志级别设置
 * 2. 应用全局的格式化器设置
 * 3. 应用全局的刷新级别设置
 * 4. 将 logger 注册到全局注册表中
 * 
 * @note
 * - 如果你使用 create() 函数创建 logger，不需要调用此函数
 * - 此函数主要用于需要自定义 logger 构造过程的场景
 * - Logger 名称必须唯一，如果已存在同名 logger 会抛出异常
 * 
 * @throws spdlog_ex 如果 logger 名称已存在
 * 
 * @par 使用示例：
 * @code
 * // 手动创建一个带有多个 sink 的 logger
 * std::vector<spdlog::sink_ptr> sinks;
 * sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
 * sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/daily.txt", 23, 59));
 * 
 * auto my_logger = std::make_shared<spdlog::logger>("my_logger", sinks.begin(), sinks.end());
 * 
 * // 初始化并注册 logger（应用全局设置）
 * spdlog::initialize_logger(my_logger);
 * 
 * // 现在可以使用 logger 了
 * my_logger->info("Logger 已初始化");
 * @endcode
 * 
 * @see create() 用于快速创建和注册 logger
 * @see register_logger() 仅注册 logger 而不应用全局设置
 */
SPDLOG_API void initialize_logger(std::shared_ptr<logger> logger);

/**
 * @brief 获取已注册的 logger
 * 
 * @param name Logger 的名称
 * @return 指向 logger 的智能指针，如果不存在则返回 nullptr
 * 
 * @details
 * 从全局注册表中查找并返回指定名称的 logger。
 * 如果 logger 不存在，返回 nullptr 而不是抛出异常。
 * 
 * @note
 * - 返回的是智能指针，可以安全地在多个地方使用
 * - 如果 logger 不存在，返回 nullptr，使用前应检查
 * - Logger 名称区分大小写
 * 
 * @par 使用示例：
 * @code
 * // 获取已存在的 logger
 * auto logger = spdlog::get("my_logger");
 * if (logger) {
 *     logger->info("Hello {}", "World");
 * } else {
 *     std::cerr << "Logger 不存在" << std::endl;
 * }
 * 
 * // 链式调用（确保 logger 存在）
 * spdlog::get("my_logger")->info("Hello {}", "World");
 * @endcode
 * 
 * @warning 如果 logger 不存在，链式调用会导致空指针解引用，使用前应检查
 * 
 * @see create() 用于创建新的 logger
 * @see register_logger() 用于注册 logger
 */
SPDLOG_API std::shared_ptr<logger> get(const std::string &name);

/**
 * @brief 设置全局格式化器
 * 
 * @param formatter 格式化器的 unique_ptr（所有权转移）
 * 
 * @details
 * 为所有已注册的 logger 设置新的格式化器。
 * 每个 logger 中的每个 sink 都会获得此格式化器的独立克隆。
 * 
 * @note
 * - 此函数会影响所有已注册的 logger
 * - 每个 sink 获得格式化器的独立副本，互不影响
 * - 新注册的 logger 也会使用此格式化器
 * - 传入的 unique_ptr 所有权会被转移
 * 
 * @par 使用示例：
 * @code
 * // 创建自定义格式化器
 * auto formatter = std::make_unique<spdlog::pattern_formatter>(
 *     "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v",
 *     spdlog::pattern_time_type::local
 * );
 * 
 * // 应用到所有 logger
 * spdlog::set_formatter(std::move(formatter));
 * @endcode
 * 
 * @see set_pattern() 使用格式字符串设置格式化器的便捷方法
 */
SPDLOG_API void set_formatter(std::unique_ptr<spdlog::formatter> formatter);

/**
 * @brief 使用格式字符串设置全局格式化器
 * 
 * @param pattern 格式化模式字符串
 * @param time_type 时间类型（本地时间或 UTC 时间），默认为本地时间
 * 
 * @details
 * 这是 set_formatter() 的便捷版本，直接使用格式字符串创建格式化器。
 * 
 * 常用的格式化标志：
 * - %Y: 年份（4位）
 * - %m: 月份（01-12）
 * - %d: 日期（01-31）
 * - %H: 小时（00-23）
 * - %M: 分钟（00-59）
 * - %S: 秒（00-59）
 * - %e: 毫秒（000-999）
 * - %l: 日志级别
 * - %n: Logger 名称
 * - %v: 实际的日志消息
 * - %t: 线程 ID
 * - %P: 进程 ID
 * - %^: 开始颜色
 * - %$: 结束颜色
 * 
 * @note
 * - 等价于 set_formatter(make_unique<pattern_formatter>(pattern, time_type))
 * - 每个 sink 会获得格式化器的独立副本
 * 
 * @par 使用示例：
 * @code
 * // 设置简单的格式
 * spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e %l : %v");
 * 
 * // 设置带颜色的格式
 * spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
 * 
 * // 使用 UTC 时间
 * spdlog::set_pattern("%Y-%m-%d %H:%M:%S %v", spdlog::pattern_time_type::utc);
 * @endcode
 * 
 * @see set_formatter() 使用自定义格式化器对象
 */
SPDLOG_API void set_pattern(std::string pattern,
                            pattern_time_type time_type = pattern_time_type::local);

/**
 * @brief 启用全局回溯支持
 * 
 * @param n_messages 要存储的消息数量
 * 
 * @details
 * 回溯（backtrace）功能会在内存中保存最近的 n 条 debug/trace 级别的日志消息。
 * 当发生错误时，可以调用 dump_backtrace() 将这些消息输出，帮助调试。
 * 
 * 工作原理：
 * 1. 启用后，debug 和 trace 级别的消息会被存储在循环缓冲区中
 * 2. 缓冲区大小为 n_messages，超过后会覆盖最旧的消息
 * 3. 调用 dump_backtrace() 时，会将缓冲区中的所有消息输出
 * 
 * @note
 * - 回溯功能对性能有轻微影响，因为需要存储消息
 * - 只有 debug 和 trace 级别的消息会被存储
 * - 适用于调试难以重现的问题
 * 
 * @par 使用示例：
 * @code
 * // 启用回溯，保存最近 32 条 debug/trace 消息
 * spdlog::enable_backtrace(32);
 * 
 * // 正常记录日志
 * spdlog::debug("调试信息 1");
 * spdlog::debug("调试信息 2");
 * spdlog::info("普通信息");  // 不会被存储
 * 
 * // 发生错误时，转储回溯信息
 * if (error_occurred) {
 *     spdlog::dump_backtrace();  // 输出之前的 debug 消息
 * }
 * @endcode
 * 
 * @see disable_backtrace() 禁用回溯功能
 * @see dump_backtrace() 输出回溯消息
 */
SPDLOG_API void enable_backtrace(size_t n_messages);

/**
 * @brief 禁用全局回溯支持
 * 
 * @details
 * 禁用回溯功能并清空回溯缓冲区。
 * 
 * @note 禁用后，之前存储的回溯消息会被清除
 * 
 * @see enable_backtrace() 启用回溯功能
 */
SPDLOG_API void disable_backtrace();

/**
 * @brief 在默认 logger 上转储回溯消息
 * 
 * @details
 * 将回溯缓冲区中存储的所有 debug/trace 消息输出到默认 logger。
 * 输出后，缓冲区会被清空。
 * 
 * @note
 * - 只影响默认 logger
 * - 输出后缓冲区会被清空，但回溯功能仍然启用
 * - 如果回溯功能未启用，此函数不会有任何效果
 * 
 * @par 使用示例：
 * @code
 * spdlog::enable_backtrace(32);
 * 
 * // 记录一些 debug 消息
 * for (int i = 0; i < 10; ++i) {
 *     spdlog::debug("步骤 {}", i);
 * }
 * 
 * // 发生错误，转储回溯信息以帮助调试
 * if (error_occurred) {
 *     spdlog::error("发生错误！");
 *     spdlog::dump_backtrace();  // 输出之前的 10 条 debug 消息
 * }
 * @endcode
 * 
 * @see enable_backtrace() 启用回溯功能
 * @see disable_backtrace() 禁用回溯功能
 */
SPDLOG_API void dump_backtrace();

/**
 * @brief 获取全局日志级别
 * 
 * @return 当前的全局日志级别
 * 
 * @details
 * 返回默认 logger 的日志级别。
 * 
 * @note 这是默认 logger 的级别，不影响其他已注册的 logger
 * 
 * @see set_level() 设置全局日志级别
 */
SPDLOG_API level::level_enum get_level();

/**
 * @brief 设置全局日志级别
 * 
 * @param log_level 要设置的日志级别
 * 
 * @details
 * 设置默认 logger 的日志级别。
 * 只有级别大于等于此级别的消息才会被记录。
 * 
 * 日志级别从低到高：
 * - trace: 最详细的调试信息
 * - debug: 调试信息
 * - info: 一般信息（默认级别）
 * - warn: 警告信息
 * - err: 错误信息
 * - critical: 严重错误
 * - off: 关闭日志
 * 
 * @note
 * - 这只影响默认 logger
 * - 其他已注册的 logger 不受影响
 * - 可以为每个 logger 单独设置级别
 * 
 * @par 使用示例：
 * @code
 * // 设置为 debug 级别，trace 消息不会被记录
 * spdlog::set_level(spdlog::level::debug);
 * 
 * spdlog::trace("这条消息不会被记录");
 * spdlog::debug("这条消息会被记录");
 * spdlog::info("这条消息会被记录");
 * 
 * // 设置为 off，关闭所有日志
 * spdlog::set_level(spdlog::level::off);
 * @endcode
 * 
 * @see get_level() 获取当前日志级别
 * @see should_log() 检查某个级别是否会被记录
 */
SPDLOG_API void set_level(level::level_enum log_level);

/**
 * @brief 检查默认 logger 是否应记录指定级别的消息
 * 
 * @param log_level 要检查的日志级别
 * @return 如果该级别的消息会被记录，返回 true
 * 
 * @details
 * 检查给定的日志级别是否大于等于默认 logger 的当前级别。
 * 
 * @note 这是一个轻量级的检查，可以用于避免不必要的日志格式化开销
 * 
 * @par 使用示例：
 * @code
 * // 避免昂贵的字符串构造
 * if (spdlog::should_log(spdlog::level::debug)) {
 *     std::string expensive_debug_info = compute_debug_info();
 *     spdlog::debug("调试信息: {}", expensive_debug_info);
 * }
 * @endcode
 * 
 * @see set_level() 设置日志级别
 */
SPDLOG_API bool should_log(level::level_enum log_level);

/**
 * @brief 设置全局刷新级别
 * 
 * @param log_level 触发刷新的日志级别
 * 
 * @details
 * 设置自动刷新的触发级别。
 * 当记录的消息级别大于等于此级别时，会自动刷新所有缓冲区。
 * 
 * 刷新操作会将缓冲区中的所有日志立即写入到目标（文件、控制台等）。
 * 
 * @note
 * - 默认情况下，只有在 logger 销毁时才会刷新
 * - 设置刷新级别可以确保重要消息立即写入
 * - 频繁刷新会影响性能，建议只对重要级别（如 error）启用
 * 
 * @par 使用示例：
 * @code
 * // 当记录 error 或更高级别的消息时自动刷新
 * spdlog::flush_on(spdlog::level::err);
 * 
 * spdlog::info("这条消息不会触发刷新");
 * spdlog::error("这条消息会触发刷新");  // 立即写入文件
 * @endcode
 * 
 * @see flush_every() 设置周期性刷新
 */
SPDLOG_API void flush_on(level::level_enum log_level);

/**
 * @brief 启动或重启周期性刷新线程
 * 
 * @tparam Rep 时间间隔的数值类型
 * @tparam Period 时间间隔的周期类型
 * @param interval 刷新间隔（如 std::chrono::seconds(5)）
 * 
 * @details
 * 启动一个后台线程，定期刷新所有已注册 logger 的缓冲区。
 * 
 * @warning
 * - 只有在所有 logger 都是线程安全的（_mt 版本）时才能使用！
 * - 如果使用单线程版本的 logger（_st），会导致数据竞争
 * - 周期性刷新会影响性能，应根据需求权衡
 * 
 * @note
 * - 如果已经有刷新线程在运行，会先停止旧线程再启动新线程
 * - 刷新线程会在程序退出时自动停止
 * 
 * @par 使用示例：
 * @code
 * // 每 5 秒刷新一次所有 logger
 * spdlog::flush_every(std::chrono::seconds(5));
 * 
 * // 每 100 毫秒刷新一次（高频率，影响性能）
 * spdlog::flush_every(std::chrono::milliseconds(100));
 * @endcode
 * 
 * @see flush_on() 设置基于级别的自动刷新
 */
template <typename Rep, typename Period>
inline void flush_every(std::chrono::duration<Rep, Period> interval) {
    details::registry::instance().flush_every(interval);
}

/**
 * @brief 设置全局错误处理器
 * 
 * @param handler 错误处理函数指针，接受错误消息字符串作为参数
 * 
 * @details
 * 设置一个全局的错误处理函数，当 spdlog 内部发生错误时会调用此函数。
 * 
 * 可能触发错误处理器的情况：
 * - 写入文件失败（磁盘满、权限不足等）
 * - 网络 sink 连接失败
 * - 格式化错误
 * - 其他 I/O 错误
 * 
 * @note
 * - 默认的错误处理器会将错误输出到 stderr
 * - 错误处理器应该是轻量级的，避免阻塞或抛出异常
 * - 错误处理器在所有 logger 之间共享
 * 
 * @par 使用示例：
 * @code
 * // 设置自定义错误处理器
 * spdlog::set_error_handler([](const std::string& msg) {
 *     std::cerr << "spdlog 错误: " << msg << std::endl;
 *     // 可以记录到错误日志文件
 *     // 可以发送告警通知
 * });
 * 
 * // 恢复默认错误处理器
 * spdlog::set_error_handler(nullptr);
 * @endcode
 * 
 * @warning 错误处理器中不应该再调用 spdlog 的日志函数，否则可能导致无限递归
 */
SPDLOG_API void set_error_handler(void (*handler)(const std::string &msg));

/**
 * @brief 注册一个 logger 到全局注册表
 * 
 * @param logger 要注册的 logger 智能指针
 * 
 * @details
 * 将 logger 添加到全局注册表中，使其可以通过 get() 函数获取。
 * 
 * @throws spdlog_ex 如果已存在同名的 logger
 * 
 * @note
 * - Logger 名称必须唯一
 * - 注册后可以通过 get(name) 获取
 * - 此函数不会应用全局设置，如需应用请使用 initialize_logger()
 * 
 * @par 使用示例：
 * @code
 * // 创建 logger
 * auto logger = std::make_shared<spdlog::logger>("my_logger", sink);
 * 
 * // 注册到全局注册表
 * spdlog::register_logger(logger);
 * 
 * // 现在可以通过名称获取
 * auto same_logger = spdlog::get("my_logger");
 * @endcode
 * 
 * @see initialize_logger() 注册并应用全局设置
 * @see register_or_replace() 注册或替换已存在的 logger
 * @see get() 获取已注册的 logger
 */
SPDLOG_API void register_logger(std::shared_ptr<logger> logger);

/**
 * @brief 注册或替换一个 logger
 * 
 * @param logger 要注册的 logger 智能指针
 * 
 * @details
 * 将 logger 添加到全局注册表中。
 * 如果已存在同名的 logger，会被新的 logger 替换。
 * 
 * @note
 * - 与 register_logger() 不同，此函数不会在名称冲突时抛出异常
 * - 旧的 logger 会被新的 logger 替换
 * - 如果其他地方还持有旧 logger 的引用，旧 logger 仍然有效
 * 
 * @par 使用示例：
 * @code
 * // 第一次注册
 * auto logger1 = std::make_shared<spdlog::logger>("my_logger", sink1);
 * spdlog::register_or_replace(logger1);
 * 
 * // 替换为新的 logger（不会抛出异常）
 * auto logger2 = std::make_shared<spdlog::logger>("my_logger", sink2);
 * spdlog::register_or_replace(logger2);
 * 
 * // 现在 get() 返回 logger2
 * auto current = spdlog::get("my_logger");  // 返回 logger2
 * @endcode
 * 
 * @see register_logger() 注册 logger（名称冲突时抛出异常）
 */
SPDLOG_API void register_or_replace(std::shared_ptr<logger> logger);

/**
 * @brief 对所有已注册的 logger 应用用户定义的函数
 * 
 * @param fun 要应用的函数，接受 logger 智能指针作为参数
 * 
 * @details
 * 遍历所有已注册的 logger，对每个 logger 调用提供的函数。
 * 这对于批量操作所有 logger 很有用。
 * 
 * @note
 * - 函数会在持有注册表锁的情况下执行，应保持轻量级
 * - 不要在回调函数中注册或删除 logger，可能导致死锁
 * 
 * @par 使用示例：
 * @code
 * // 刷新所有 logger
 * spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) {
 *     l->flush();
 * });
 * 
 * // 设置所有 logger 的级别
 * spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) {
 *     l->set_level(spdlog::level::debug);
 * });
 * 
 * // 统计所有 logger
 * int count = 0;
 * spdlog::apply_all([&count](std::shared_ptr<spdlog::logger> l) {
 *     count++;
 * });
 * std::cout << "共有 " << count << " 个 logger" << std::endl;
 * @endcode
 * 
 * @warning 回调函数中不要执行耗时操作，会阻塞其他线程访问注册表
 */
SPDLOG_API void apply_all(const std::function<void(std::shared_ptr<logger>)> &fun);

/**
 * @brief 从注册表中删除指定的 logger
 * 
 * @param name 要删除的 logger 名称
 * 
 * @details
 * 从全局注册表中移除指定名称的 logger。
 * 
 * @note
 * - 如果 logger 不存在，此函数不会有任何效果
 * - 删除后，get(name) 将返回 nullptr
 * - 如果其他地方还持有该 logger 的引用，logger 对象仍然有效
 * - 只是从注册表中移除，不会销毁 logger 对象
 * 
 * @par 使用示例：
 * @code
 * // 创建并注册 logger
 * auto logger = spdlog::stdout_color_mt("temp_logger");
 * logger->info("临时日志");
 * 
 * // 从注册表中删除
 * spdlog::drop("temp_logger");
 * 
 * // 现在无法通过 get() 获取
 * auto removed = spdlog::get("temp_logger");  // 返回 nullptr
 * 
 * // 但如果还持有引用，logger 仍然可用
 * logger->info("仍然可以使用");  // 有效
 * @endcode
 * 
 * @see drop_all() 删除所有 logger
 */
SPDLOG_API void drop(const std::string &name);

/**
 * @brief 从注册表中删除所有 logger
 * 
 * @details
 * 清空全局注册表，移除所有已注册的 logger。
 * 
 * @note
 * - 删除后，所有 get() 调用都会返回 nullptr
 * - 如果其他地方还持有 logger 的引用，这些 logger 对象仍然有效
 * - 默认 logger 也会被删除
 * 
 * @par 使用示例：
 * @code
 * // 创建多个 logger
 * auto logger1 = spdlog::stdout_color_mt("logger1");
 * auto logger2 = spdlog::basic_logger_mt("logger2", "log.txt");
 * 
 * // 删除所有 logger
 * spdlog::drop_all();
 * 
 * // 现在注册表为空
 * auto removed1 = spdlog::get("logger1");  // 返回 nullptr
 * auto removed2 = spdlog::get("logger2");  // 返回 nullptr
 * @endcode
 * 
 * @see drop() 删除单个 logger
 * @see shutdown() 停止所有线程并清理
 */
SPDLOG_API void drop_all();

/**
 * @brief 停止所有后台线程并清理注册表
 * 
 * @details
 * 执行完整的清理操作：
 * 1. 停止所有由 spdlog 启动的后台线程（如周期性刷新线程、异步日志线程）
 * 2. 刷新所有 logger 的缓冲区
 * 3. 清空全局注册表
 * 
 * @note
 * - 通常在程序退出前调用，确保所有日志都被写入
 * - 调用后，spdlog 处于未初始化状态，可以重新初始化
 * - 如果不调用此函数，程序退出时也会自动清理
 * 
 * @par 使用示例：
 * @code
 * int main() {
 *     auto logger = spdlog::stdout_color_mt("console");
 *     logger->info("程序开始");
 *     
 *     // ... 程序逻辑 ...
 *     
 *     logger->info("程序结束");
 *     
 *     // 确保所有日志都被写入
 *     spdlog::shutdown();
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @see drop_all() 仅清空注册表，不停止线程
 */
SPDLOG_API void shutdown();

/**
 * @brief 设置是否自动注册 logger
 * 
 * @param automatic_registration true 表示自动注册，false 表示不自动注册
 * 
 * @details
 * 控制使用 create() 或 create_async() 创建的 logger 是否自动注册到全局注册表。
 * 
 * @note
 * - 默认情况下，自动注册是启用的
 * - 禁用自动注册后，需要手动调用 register_logger() 来注册 logger
 * - 这对于需要完全控制 logger 生命周期的场景很有用
 * 
 * @par 使用示例：
 * @code
 * // 禁用自动注册
 * spdlog::set_automatic_registration(false);
 * 
 * // 创建 logger（不会自动注册）
 * auto logger = spdlog::create<spdlog::sinks::stdout_color_sink_mt>("my_logger");
 * 
 * // 无法通过 get() 获取
 * auto not_found = spdlog::get("my_logger");  // 返回 nullptr
 * 
 * // 需要手动注册
 * spdlog::register_logger(logger);
 * 
 * // 现在可以获取了
 * auto found = spdlog::get("my_logger");  // 返回 logger
 * @endcode
 */
SPDLOG_API void set_automatic_registration(bool automatic_registration);

/**
 * @name 默认 Logger API
 * @brief 使用默认 logger 的便捷 API
 * 
 * @details
 * spdlog 提供了一组便捷的全局函数，用于快速记录日志。
 * 这些函数使用默认的 logger（stdout_color_mt），无需显式创建 logger。
 * 
 * 默认 logger 特点：
 * - 名称为空字符串
 * - 输出到标准输出（控制台）
 * - 支持彩色输出
 * - 线程安全（多线程版本）
 * 
 * 访问默认 logger：
 * @code
 * // 获取默认 logger 对象
 * auto logger = spdlog::default_logger();
 * 
 * // 向默认 logger 添加额外的 sink
 * logger->sinks().push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/daily.txt", 23, 59));
 * @endcode
 * 
 * 替换默认 logger：
 * @code
 * // 创建新的 logger
 * auto file_logger = spdlog::basic_logger_mt("file", "logs/app.log");
 * 
 * // 设置为默认 logger
 * spdlog::set_default_logger(file_logger);
 * 
 * // 现在全局 API 使用文件 logger
 * spdlog::info("这条消息会写入文件");
 * @endcode
 * 
 * @warning
 * 默认 API 是线程安全的（对于 _mt logger），但是：
 * - set_default_logger() 不应该与默认 API 并发使用
 * - 不要在一个线程中调用 set_default_logger() 的同时在另一个线程中调用 spdlog::info()
 * - 这可能导致未定义行为或崩溃
 * 
 * @par 使用示例：
 * @code
 * #include "spdlog/spdlog.h"
 * 
 * int main() {
 *     // 直接使用全局 API
 *     spdlog::info("欢迎使用 spdlog!");
 *     spdlog::error("发生了错误: {}", 42);
 *     spdlog::warn("这是一个警告");
 *     
 *     // 设置日志级别
 *     spdlog::set_level(spdlog::level::debug);
 *     spdlog::debug("调试信息");
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @{
 */

/**
 * @brief 获取默认 logger
 * @return 指向默认 logger 的智能指针
 * @note 返回的是智能指针，可以安全地存储和使用
 */
SPDLOG_API std::shared_ptr<spdlog::logger> default_logger();

/**
 * @brief 获取默认 logger 的原始指针
 * @return 指向默认 logger 的原始指针
 * @note 
 * - 返回原始指针，性能略优于智能指针版本
 * - 不要删除返回的指针
 * - 主要用于内部实现和性能关键路径
 */
SPDLOG_API spdlog::logger *default_logger_raw();

/**
 * @brief 设置默认 logger
 * @param default_logger 要设置为默认的 logger
 * 
 * @details
 * 替换当前的默认 logger。
 * 之后所有全局 API（如 spdlog::info()）都会使用新的 logger。
 * 
 * @warning 不要在多线程环境下与全局 API 并发调用此函数
 * 
 * @par 使用示例：
 * @code
 * // 创建文件 logger
 * auto file_logger = spdlog::basic_logger_mt("file", "app.log");
 * 
 * // 设置为默认 logger
 * spdlog::set_default_logger(file_logger);
 * 
 * // 现在全局 API 写入文件
 * spdlog::info("写入文件");
 * @endcode
 */
SPDLOG_API void set_default_logger(std::shared_ptr<spdlog::logger> default_logger);
/** @} */

/**
 * @brief 根据环境变量初始化 logger 的日志级别
 * 
 * @param logger 要初始化的 logger
 * 
 * @details
 * 从环境变量 SPDLOG_LEVEL 读取日志级别并应用到指定的 logger。
 * 
 * 环境变量格式：
 * - SPDLOG_LEVEL=info: 设置所有 logger 为 info 级别
 * - SPDLOG_LEVEL=logger1=debug,logger2=trace: 为不同 logger 设置不同级别
 * 
 * @note
 * - 主要用于手动创建的 logger
 * - 使用 create() 创建的 logger 会自动应用环境变量设置
 * 
 * @par 使用示例：
 * @code
 * // 设置环境变量（在程序外部或启动时）
 * // export SPDLOG_LEVEL=debug
 * 
 * // 创建 logger
 * auto logger = std::make_shared<spdlog::logger>("my_logger", sink);
 * 
 * // 应用环境变量设置
 * spdlog::apply_logger_env_levels(logger);
 * 
 * // 现在 logger 的级别为 debug
 * @endcode
 * 
 * @see initialize_logger() 应用所有全局设置（包括环境变量）
 */
SPDLOG_API void apply_logger_env_levels(std::shared_ptr<logger> logger);

template <typename... Args>
inline void log(source_loc source,
                level::level_enum lvl,
                format_string_t<Args...> fmt,
                Args &&...args) {
    default_logger_raw()->log(source, lvl, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void log(level::level_enum lvl, format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->log(source_loc{}, lvl, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void trace(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->trace(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->debug(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->info(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->warn(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->error(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void critical(format_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->critical(fmt, std::forward<Args>(args)...);
}

template <typename T>
inline void log(source_loc source, level::level_enum lvl, const T &msg) {
    default_logger_raw()->log(source, lvl, msg);
}

template <typename T>
inline void log(level::level_enum lvl, const T &msg) {
    default_logger_raw()->log(lvl, msg);
}

#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT
template <typename... Args>
inline void log(source_loc source,
                level::level_enum lvl,
                wformat_string_t<Args...> fmt,
                Args &&...args) {
    default_logger_raw()->log(source, lvl, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void log(level::level_enum lvl, wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->log(source_loc{}, lvl, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void trace(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->trace(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->debug(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->info(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->warn(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->error(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void critical(wformat_string_t<Args...> fmt, Args &&...args) {
    default_logger_raw()->critical(fmt, std::forward<Args>(args)...);
}
#endif

template <typename T>
inline void trace(const T &msg) {
    default_logger_raw()->trace(msg);
}

template <typename T>
inline void debug(const T &msg) {
    default_logger_raw()->debug(msg);
}

template <typename T>
inline void info(const T &msg) {
    default_logger_raw()->info(msg);
}

template <typename T>
inline void warn(const T &msg) {
    default_logger_raw()->warn(msg);
}

template <typename T>
inline void error(const T &msg) {
    default_logger_raw()->error(msg);
}

template <typename T>
inline void critical(const T &msg) {
    default_logger_raw()->critical(msg);
}

}  // namespace spdlog

//
// 根据全局级别在编译时启用/禁用日志调用。
//
// 将 SPDLOG_ACTIVE_LEVEL 定义为以下之一（在包含 spdlog.h 之前）:
// SPDLOG_LEVEL_TRACE,
// SPDLOG_LEVEL_DEBUG,
// SPDLOG_LEVEL_INFO,
// SPDLOG_LEVEL_WARN,
// SPDLOG_LEVEL_ERROR,
// SPDLOG_LEVEL_CRITICAL,
// SPDLOG_LEVEL_OFF
//

#ifndef SPDLOG_NO_SOURCE_LOC
#define SPDLOG_LOGGER_CALL(logger, level, ...) \
    (logger)->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, level, __VA_ARGS__)
#else
#define SPDLOG_LOGGER_CALL(logger, level, ...) \
    (logger)->log(spdlog::source_loc{}, level, __VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define SPDLOG_LOGGER_TRACE(logger, ...) \
    SPDLOG_LOGGER_CALL(logger, spdlog::level::trace, __VA_ARGS__)
#define SPDLOG_TRACE(...) SPDLOG_LOGGER_TRACE(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_TRACE(logger, ...) (void)0
#define SPDLOG_TRACE(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define SPDLOG_LOGGER_DEBUG(logger, ...) \
    SPDLOG_LOGGER_CALL(logger, spdlog::level::debug, __VA_ARGS__)
#define SPDLOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_DEBUG(logger, ...) (void)0
#define SPDLOG_DEBUG(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#define SPDLOG_LOGGER_INFO(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::info, __VA_ARGS__)
#define SPDLOG_INFO(...) SPDLOG_LOGGER_INFO(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_INFO(logger, ...) (void)0
#define SPDLOG_INFO(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#define SPDLOG_LOGGER_WARN(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::warn, __VA_ARGS__)
#define SPDLOG_WARN(...) SPDLOG_LOGGER_WARN(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_WARN(logger, ...) (void)0
#define SPDLOG_WARN(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#define SPDLOG_LOGGER_ERROR(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::err, __VA_ARGS__)
#define SPDLOG_ERROR(...) SPDLOG_LOGGER_ERROR(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_ERROR(logger, ...) (void)0
#define SPDLOG_ERROR(...) (void)0
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#define SPDLOG_LOGGER_CRITICAL(logger, ...) \
    SPDLOG_LOGGER_CALL(logger, spdlog::level::critical, __VA_ARGS__)
#define SPDLOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(spdlog::default_logger_raw(), __VA_ARGS__)
#else
#define SPDLOG_LOGGER_CRITICAL(logger, ...) (void)0
#define SPDLOG_CRITICAL(...) (void)0
#endif

#ifdef SPDLOG_HEADER_ONLY
#include "spdlog-inl.h"
#endif

#endif  // SPDLOG_H
