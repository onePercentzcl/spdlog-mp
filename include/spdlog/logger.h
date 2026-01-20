/**
 * @file logger.h
 * @brief Logger 类定义
 * @author Gabi Melman 及 spdlog 贡献者
 * @copyright 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
 *            根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)
 */

#pragma once

/**
 * @brief 线程安全的日志记录器类（除了 set_error_handler() 方法）
 * 
 * Logger 类是 spdlog 的核心组件，负责管理日志消息的记录和分发。
 * 
 * @details
 * Logger 包含以下核心组件：
 * - 名称：用于标识 logger 的唯一名称
 * - 日志级别：控制哪些消息应该被记录
 * - Sink 向量：std::shared_ptr<sink> 的集合，定义日志输出目标
 * - 格式化器：用于格式化日志消息的对象
 * 
 * 日志记录流程：
 * 1. 检查消息的日志级别是否满足 logger 的最低级别要求
 * 2. 如果满足，调用所有已注册的 sink 来处理消息
 * 3. 每个 sink 使用自己的私有格式化器副本来格式化消息
 * 4. 格式化后的消息被发送到各自的目标（控制台、文件等）
 * 
 * @note 每个 sink 拥有独立的格式化器实例，这样可以：
 *       - 缓存部分格式化数据以提高性能
 *       - 为不同的输出目标使用不同的格式
 * 
 * @thread_safety 除了 set_error_handler() 方法外，所有方法都是线程安全的
 */

#include <spdlog/common.h>
#include <spdlog/details/backtracer.h>
#include <spdlog/details/log_msg.h>

#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT
#ifndef _WIN32
#error SPDLOG_WCHAR_TO_UTF8_SUPPORT only supported on windows
#endif
#include <spdlog/details/os.h>
#endif

#include <vector>

#ifndef SPDLOG_NO_EXCEPTIONS
#define SPDLOG_LOGGER_CATCH(location)                                                 \
    catch (const std::exception &ex) {                                                \
        if (location.filename) {                                                      \
            err_handler_(fmt_lib::format(SPDLOG_FMT_STRING("{} [{}({})]"), ex.what(), \
                                         location.filename, location.line));          \
        } else {                                                                      \
            err_handler_(ex.what());                                                  \
        }                                                                             \
    }                                                                                 \
    catch (...) {                                                                     \
        err_handler_("Rethrowing unknown exception in logger");                       \
        throw;                                                                        \
    }
#else
#define SPDLOG_LOGGER_CATCH(location)
#endif

namespace spdlog {

/**
 * @class logger
 * @brief 日志记录器类
 * 
 * Logger 是 spdlog 的核心类，提供了完整的日志记录功能。
 * 支持多种日志级别、多个输出目标、自定义格式化以及回溯功能。
 */
class SPDLOG_API logger {
public:
    /**
     * @brief 构造一个空的 logger（不包含任何 sink）
     * @param name Logger 的名称
     */
    explicit logger(std::string name)
        : name_(std::move(name)),
          sinks_() {}

    /**
     * @brief 使用迭代器范围构造 logger
     * @tparam It 迭代器类型
     * @param name Logger 的名称
     * @param begin Sink 容器的起始迭代器
     * @param end Sink 容器的结束迭代器
     */
    template <typename It>
    logger(std::string name, It begin, It end)
        : name_(std::move(name)),
          sinks_(begin, end) {}

    /**
     * @brief 使用单个 sink 构造 logger
     * @param name Logger 的名称
     * @param single_sink 单个 sink 的共享指针
     */
    logger(std::string name, sink_ptr single_sink)
        : logger(std::move(name), {std::move(single_sink)}) {}

    /**
     * @brief 使用 sink 初始化列表构造 logger
     * @param name Logger 的名称
     * @param sinks Sink 的初始化列表
     */
    logger(std::string name, sinks_init_list sinks)
        : logger(std::move(name), sinks.begin(), sinks.end()) {}

    /**
     * @brief 虚析构函数
     */
    virtual ~logger() = default;

    /**
     * @brief 拷贝构造函数
     * @param other 要拷贝的 logger 对象
     */
    logger(const logger &other);
    
    /**
     * @brief 移动构造函数
     * @param other 要移动的 logger 对象
     */
    logger(logger &&other) SPDLOG_NOEXCEPT;
    
    /**
     * @brief 赋值运算符
     * @param other 要赋值的 logger 对象
     * @return 当前 logger 的引用
     */
    logger &operator=(logger other) SPDLOG_NOEXCEPT;
    
    /**
     * @brief 交换两个 logger 对象
     * @param other 要交换的 logger 对象
     */
    void swap(spdlog::logger &other) SPDLOG_NOEXCEPT;

    /**
     * @brief 记录带有源位置信息的格式化日志消息
     * @tparam Args 可变参数类型
     * @param loc 源代码位置信息
     * @param lvl 日志级别
     * @param fmt 格式化字符串
     * @param args 格式化参数
     */
    template <typename... Args>
    void log(source_loc loc, level::level_enum lvl, format_string_t<Args...> fmt, Args &&...args) {
        log_(loc, lvl, details::to_string_view(fmt), std::forward<Args>(args)...);
    }

    /**
     * @brief 记录格式化日志消息（不包含源位置信息）
     * @tparam Args 可变参数类型
     * @param lvl 日志级别
     * @param fmt 格式化字符串
     * @param args 格式化参数
     */
    template <typename... Args>
    void log(level::level_enum lvl, format_string_t<Args...> fmt, Args &&...args) {
        log(source_loc{}, lvl, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录简单消息（不使用格式化）
     * @tparam T 消息类型
     * @param lvl 日志级别
     * @param msg 日志消息
     */
    template <typename T>
    void log(level::level_enum lvl, const T &msg) {
        log(source_loc{}, lvl, msg);
    }

    /**
     * @brief 记录不能静态转换为格式字符串的类型
     * @tparam T 消息类型
     * @param loc 源代码位置信息
     * @param lvl 日志级别
     * @param msg 日志消息
     * @note 此重载用于处理不能直接转换为 string_view 或 wstring_view 的类型
     */
    template <class T,
              typename std::enable_if<!is_convertible_to_any_format_string<const T &>::value,
                                      int>::type = 0>
    void log(source_loc loc, level::level_enum lvl, const T &msg) {
        log(loc, lvl, "{}", msg);
    }

    void log(log_clock::time_point log_time,
             source_loc loc,
             level::level_enum lvl,
             string_view_t msg) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }

        details::log_msg log_msg(log_time, loc, name_, lvl, msg);
        log_it_(log_msg, log_enabled, traceback_enabled);
    }

    void log(source_loc loc, level::level_enum lvl, string_view_t msg) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }

        details::log_msg log_msg(loc, name_, lvl, msg);
        log_it_(log_msg, log_enabled, traceback_enabled);
    }

    void log(level::level_enum lvl, string_view_t msg) { log(source_loc{}, lvl, msg); }

    /**
     * @name 便捷日志方法
     * @brief 各个日志级别的便捷方法
     * @details 
     * 这些方法是 log() 方法的便捷包装，每个方法对应一个特定的日志级别。
     * 使用这些方法可以使代码更简洁、更易读。
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 使用便捷方法
     * logger->trace("详细的跟踪信息");
     * logger->debug("调试信息: {}", debug_value);
     * logger->info("程序启动");
     * logger->warn("警告: 配置文件不存在，使用默认值");
     * logger->error("错误: 无法打开文件 {}", filename);
     * logger->critical("严重错误: 系统即将崩溃");
     * 
     * // 等价于使用 log() 方法
     * logger->log(spdlog::level::info, "程序启动");
     * @endcode
     * 
     * @{
     */
    
    /**
     * @brief 记录 trace 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Trace 是最详细的日志级别，用于追踪程序执行的每一步
     */
    template <typename... Args>
    void trace(format_string_t<Args...> fmt, Args &&...args) {
        log(level::trace, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录 debug 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Debug 用于调试信息，通常在开发阶段使用
     */
    template <typename... Args>
    void debug(format_string_t<Args...> fmt, Args &&...args) {
        log(level::debug, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录 info 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Info 用于一般信息，记录程序正常运行的重要事件
     */
    template <typename... Args>
    void info(format_string_t<Args...> fmt, Args &&...args) {
        log(level::info, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录 warn 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Warn 用于警告信息，表示潜在问题但不影响程序运行
     */
    template <typename... Args>
    void warn(format_string_t<Args...> fmt, Args &&...args) {
        log(level::warn, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录 error 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Error 用于错误信息，表示发生了错误但程序可以继续运行
     */
    template <typename... Args>
    void error(format_string_t<Args...> fmt, Args &&...args) {
        log(level::err, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录 critical 级别的格式化消息
     * @tparam Args 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note Critical 用于严重错误，表示发生了严重问题，可能导致程序崩溃
     */
    template <typename... Args>
    void critical(format_string_t<Args...> fmt, Args &&...args) {
        log(level::critical, fmt, std::forward<Args>(args)...);
    }
    /** @} */

#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT
    template <typename... Args>
    void log(source_loc loc, level::level_enum lvl, wformat_string_t<Args...> fmt, Args &&...args) {
        log_(loc, lvl, details::to_string_view(fmt), std::forward<Args>(args)...);
    }

    template <typename... Args>
    void log(level::level_enum lvl, wformat_string_t<Args...> fmt, Args &&...args) {
        log(source_loc{}, lvl, fmt, std::forward<Args>(args)...);
    }

    void log(log_clock::time_point log_time,
             source_loc loc,
             level::level_enum lvl,
             wstring_view_t msg) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }

        memory_buf_t buf;
        details::os::wstr_to_utf8buf(wstring_view_t(msg.data(), msg.size()), buf);
        details::log_msg log_msg(log_time, loc, name_, lvl, string_view_t(buf.data(), buf.size()));
        log_it_(log_msg, log_enabled, traceback_enabled);
    }

    void log(source_loc loc, level::level_enum lvl, wstring_view_t msg) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }

        memory_buf_t buf;
        details::os::wstr_to_utf8buf(wstring_view_t(msg.data(), msg.size()), buf);
        details::log_msg log_msg(loc, name_, lvl, string_view_t(buf.data(), buf.size()));
        log_it_(log_msg, log_enabled, traceback_enabled);
    }

    void log(level::level_enum lvl, wstring_view_t msg) { log(source_loc{}, lvl, msg); }

    template <typename... Args>
    void trace(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::err, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void critical(wformat_string_t<Args...> fmt, Args &&...args) {
        log(level::critical, fmt, std::forward<Args>(args)...);
    }
#endif

    /**
     * @name 非格式化日志方法
     * @brief 记录简单消息（不使用格式化）
     * @details 
     * 这些方法用于记录不需要格式化的简单消息。
     * 相比格式化版本，这些方法性能更好，因为跳过了格式化步骤。
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 记录简单字符串
     * logger->info("程序启动");
     * 
     * // 记录数字
     * int value = 42;
     * logger->debug(value);
     * 
     * // 记录自定义类型（需要重载 operator<<）
     * MyClass obj;
     * logger->info(obj);
     * @endcode
     * 
     * @{
     */
    
    /**
     * @brief 记录 trace 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void trace(const T &msg) {
        log(level::trace, msg);
    }

    /**
     * @brief 记录 debug 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void debug(const T &msg) {
        log(level::debug, msg);
    }

    /**
     * @brief 记录 info 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void info(const T &msg) {
        log(level::info, msg);
    }

    /**
     * @brief 记录 warn 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void warn(const T &msg) {
        log(level::warn, msg);
    }

    /**
     * @brief 记录 error 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void error(const T &msg) {
        log(level::err, msg);
    }

    /**
     * @brief 记录 critical 级别的简单消息
     * @tparam T 消息类型
     * @param msg 日志消息
     */
    template <typename T>
    void critical(const T &msg) {
        log(level::critical, msg);
    }
    /** @} */

    /**
     * @brief 检查是否应该记录指定级别的消息
     * @param msg_level 要检查的日志级别
     * @return 如果该级别的消息会被记录，返回 true
     * 
     * @details
     * 此方法检查给定的日志级别是否大于等于 logger 的当前级别。
     * 这是一个轻量级的检查，可以用于避免不必要的日志格式化开销。
     * 
     * @note
     * - 使用原子操作读取级别，线程安全
     * - 使用 memory_order_relaxed 以获得最佳性能
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * logger->set_level(spdlog::level::info);
     * 
     * // 避免昂贵的字符串构造
     * if (logger->should_log(spdlog::level::debug)) {
     *     std::string expensive_debug_info = compute_debug_info();
     *     logger->debug("调试信息: {}", expensive_debug_info);
     * }
     * // 由于级别是 info，上面的代码块不会执行
     * @endcode
     */
    bool should_log(level::level_enum msg_level) const {
        return msg_level >= level_.load(std::memory_order_relaxed);
    }

    /**
     * @brief 检查回溯功能是否已启用
     * @return 如果回溯功能已启用，返回 true
     * 
     * @details
     * 回溯功能会在内存中保存最近的 debug/trace 级别的日志消息。
     * 
     * @see enable_backtrace() 启用回溯功能
     * @see disable_backtrace() 禁用回溯功能
     * @see dump_backtrace() 输出回溯消息
     */
    bool should_backtrace() const { return tracer_.enabled(); }

    /**
     * @brief 设置 logger 的日志级别
     * @param log_level 要设置的日志级别
     * 
     * @details
     * 只有级别大于等于此级别的消息才会被记录。
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 设置为 debug 级别
     * logger->set_level(spdlog::level::debug);
     * logger->trace("不会被记录");
     * logger->debug("会被记录");
     * 
     * // 设置为 off，关闭所有日志
     * logger->set_level(spdlog::level::off);
     * @endcode
     */
    void set_level(level::level_enum log_level);

    /**
     * @brief 获取 logger 的当前日志级别
     * @return 当前的日志级别
     */
    level::level_enum level() const;

    /**
     * @brief 获取 logger 的名称
     * @return Logger 名称的常量引用
     */
    const std::string &name() const;

    /**
     * @brief 为此 logger 的所有 sink 设置格式化器
     * @param f 格式化器的 unique_ptr（所有权转移）
     * 
     * @details
     * 每个 sink 将获得格式化器对象的独立克隆。
     * 这允许每个 sink 缓存格式化数据并独立工作。
     * 
     * @note 传入的 unique_ptr 所有权会被转移
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 创建自定义格式化器
     * auto formatter = std::make_unique<spdlog::pattern_formatter>(
     *     "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
     *     spdlog::pattern_time_type::local
     * );
     * 
     * // 应用到 logger
     * logger->set_formatter(std::move(formatter));
     * @endcode
     * 
     * @see set_pattern() 使用格式字符串设置格式化器的便捷方法
     */
    void set_formatter(std::unique_ptr<formatter> f);

    /**
     * @brief 使用格式字符串设置格式化器
     * @param pattern 格式化模式字符串
     * @param time_type 时间类型（本地时间或 UTC 时间），默认为本地时间
     * 
     * @details
     * 这是 set_formatter() 的便捷版本，直接使用格式字符串创建格式化器。
     * 等价于：set_formatter(make_unique<pattern_formatter>(pattern, time_type))
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
     * @note 每个 sink 会获得格式化器的独立副本
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 设置简单的格式
     * logger->set_pattern("%Y-%m-%d %H:%M:%S.%e %l : %v");
     * 
     * // 设置带颜色和 logger 名称的格式
     * logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
     * 
     * // 使用 UTC 时间
     * logger->set_pattern("%Y-%m-%d %H:%M:%S %v", spdlog::pattern_time_type::utc);
     * @endcode
     * 
     * @see set_formatter() 使用自定义格式化器对象
     */
    void set_pattern(std::string pattern, pattern_time_type time_type = pattern_time_type::local);

    /**
     * @name 回溯功能
     * @brief 调试辅助功能，用于捕获和输出历史日志
     * @details 
     * 回溯功能会在内存中保存最近的 debug/trace 级别的日志消息。
     * 当发生错误时，可以输出这些历史消息来帮助调试。
     * 
     * 工作原理：
     * 1. 启用回溯后，debug 和 trace 级别的消息会被存储在循环缓冲区中
     * 2. 缓冲区大小固定，超过后会覆盖最旧的消息
     * 3. 调用 dump_backtrace() 时，会将缓冲区中的所有消息输出
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 启用回溯，保存最近 32 条 debug/trace 消息
     * logger->enable_backtrace(32);
     * 
     * // 正常记录日志
     * for (int i = 0; i < 100; ++i) {
     *     logger->debug("处理步骤 {}", i);
     *     // ... 处理逻辑 ...
     * }
     * 
     * // 发生错误时，转储回溯信息
     * if (error_occurred) {
     *     logger->error("发生错误！");
     *     logger->dump_backtrace();  // 输出最近 32 条 debug 消息
     * }
     * 
     * // 禁用回溯
     * logger->disable_backtrace();
     * @endcode
     * 
     * @{
     */
    
    /**
     * @brief 启用回溯功能
     * @param n_messages 要存储的消息数量
     * 
     * @details
     * 分配一个循环缓冲区来存储最近的 debug/trace 消息。
     * 
     * @note
     * - 回溯功能对性能有轻微影响
     * - 只有 debug 和 trace 级别的消息会被存储
     * - 适用于调试难以重现的问题
     */
    void enable_backtrace(size_t n_messages);
    
    /**
     * @brief 禁用回溯功能
     * 
     * @details
     * 禁用回溯功能并释放回溯缓冲区。
     * 
     * @note 禁用后，之前存储的回溯消息会被清除
     */
    void disable_backtrace();
    
    /**
     * @brief 输出回溯消息
     * 
     * @details
     * 将回溯缓冲区中存储的所有 debug/trace 消息输出到此 logger。
     * 输出后，缓冲区会被清空。
     * 
     * @note
     * - 输出后缓冲区会被清空，但回溯功能仍然启用
     * - 如果回溯功能未启用，此函数不会有任何效果
     */
    void dump_backtrace();
    /** @} */

    /**
     * @name 刷新功能
     * @brief 控制日志缓冲区的刷新行为
     * @details 
     * 刷新操作会将缓冲区中的所有日志立即写入到目标（文件、控制台等）。
     * 
     * @{
     */
    
    /**
     * @brief 立即刷新所有 sink 的缓冲区
     * 
     * @details
     * 强制将所有待写入的日志立即写入到目标。
     * 
     * @note
     * - 刷新操作会影响性能，不应频繁调用
     * - 通常在程序退出前或发生严重错误时调用
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::basic_logger_mt("file", "app.log");
     * 
     * logger->info("重要信息");
     * logger->flush();  // 确保立即写入文件
     * @endcode
     */
    void flush();
    
    /**
     * @brief 设置自动刷新的触发级别
     * @param log_level 触发刷新的日志级别
     * 
     * @details
     * 当记录的消息级别大于等于此级别时，会自动刷新所有缓冲区。
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::basic_logger_mt("file", "app.log");
     * 
     * // 当记录 error 或更高级别的消息时自动刷新
     * logger->flush_on(spdlog::level::err);
     * 
     * logger->info("这条消息不会触发刷新");
     * logger->error("这条消息会触发刷新");  // 立即写入文件
     * @endcode
     */
    void flush_on(level::level_enum log_level);
    
    /**
     * @brief 获取当前的刷新级别
     * @return 当前的刷新级别
     */
    level::level_enum flush_level() const;
    /** @} */

    /**
     * @name Sink 管理
     * @brief 访问和管理 logger 的输出目标
     * @{
     */
    
    /**
     * @brief 获取 sink 列表的常量引用
     * @return Sink 向量的常量引用
     * 
     * @note 返回常量引用，不能修改 sink 列表
     */
    const std::vector<sink_ptr> &sinks() const;

    /**
     * @brief 获取 sink 列表的引用
     * @return Sink 向量的引用
     * 
     * @details
     * 可以通过返回的引用添加或删除 sink。
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::stdout_color_mt("console");
     * 
     * // 添加文件 sink
     * logger->sinks().push_back(
     *     std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log")
     * );
     * 
     * // 现在日志会同时输出到控制台和文件
     * logger->info("同时输出到两个目标");
     * @endcode
     */
    std::vector<sink_ptr> &sinks();
    /** @} */

    /**
     * @brief 设置错误处理器
     * @param handler 错误处理函数
     * 
     * @details
     * 设置一个错误处理函数，当此 logger 发生错误时会调用此函数。
     * 
     * @warning 此方法不是线程安全的，应该在开始记录日志之前设置
     * 
     * @par 使用示例：
     * @code
     * auto logger = spdlog::basic_logger_mt("file", "app.log");
     * 
     * // 设置自定义错误处理器
     * logger->set_error_handler([](const std::string& msg) {
     *     std::cerr << "Logger 错误: " << msg << std::endl;
     * });
     * @endcode
     */
    void set_error_handler(err_handler);

    /**
     * @brief 克隆此 logger
     * @param logger_name 新 logger 的名称
     * @return 指向新 logger 的智能指针
     * 
     * @details
     * 创建一个新的 logger，使用相同的 sink 和配置。
     * 新 logger 会共享相同的 sink 对象（不是拷贝）。
     * 
     * @par 使用示例：
     * @code
     * auto logger1 = spdlog::stdout_color_mt("logger1");
     * logger1->set_level(spdlog::level::debug);
     * 
     * // 克隆 logger1，创建具有相同配置的 logger2
     * auto logger2 = logger1->clone("logger2");
     * 
     * // logger2 使用相同的 sink 和级别
     * logger2->debug("使用相同的配置");
     * @endcode
     */
    virtual std::shared_ptr<logger> clone(std::string logger_name);

protected:
    std::string name_;                              ///< Logger 的名称
    std::vector<sink_ptr> sinks_;                   ///< Sink 列表（输出目标）
    spdlog::level_t level_{level::info};           ///< 日志级别（默认为 info）
    spdlog::level_t flush_level_{level::off};      ///< 刷新级别（默认为 off，不自动刷新）
    err_handler custom_err_handler_{nullptr};       ///< 自定义错误处理器
    details::backtracer tracer_;                    ///< 回溯器（用于存储历史日志）

    /**
     * @brief 模板化公共 API 解析后的通用实现
     * @tparam Args 格式化参数类型
     * @param loc 源代码位置信息
     * @param lvl 日志级别
     * @param fmt 格式化字符串视图
     * @param args 格式化参数
     * 
     * @details
     * 这是所有格式化日志方法的内部实现。
     * 它会：
     * 1. 检查是否应该记录此消息
     * 2. 格式化消息
     * 3. 调用 log_it_() 进行实际的日志记录
     * 4. 捕获并处理异常
     */
    template <typename... Args>
    void log_(source_loc loc, level::level_enum lvl, string_view_t fmt, Args &&...args) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }
        SPDLOG_TRY {
            memory_buf_t buf;
#ifdef SPDLOG_USE_STD_FORMAT
            fmt_lib::vformat_to(std::back_inserter(buf), fmt, fmt_lib::make_format_args(args...));
#else
            fmt::vformat_to(fmt::appender(buf), fmt, fmt::make_format_args(args...));
#endif

            details::log_msg log_msg(loc, name_, lvl, string_view_t(buf.data(), buf.size()));
            log_it_(log_msg, log_enabled, traceback_enabled);
        }
        SPDLOG_LOGGER_CATCH(loc)
    }

#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT
    template <typename... Args>
    void log_(source_loc loc, level::level_enum lvl, wstring_view_t fmt, Args &&...args) {
        bool log_enabled = should_log(lvl);
        bool traceback_enabled = tracer_.enabled();
        if (!log_enabled && !traceback_enabled) {
            return;
        }
        SPDLOG_TRY {
            // format to wmemory_buffer and convert to utf8
            wmemory_buf_t wbuf;
            fmt_lib::vformat_to(std::back_inserter(wbuf), fmt,
                                fmt_lib::make_format_args<fmt_lib::wformat_context>(args...));

            memory_buf_t buf;
            details::os::wstr_to_utf8buf(wstring_view_t(wbuf.data(), wbuf.size()), buf);
            details::log_msg log_msg(loc, name_, lvl, string_view_t(buf.data(), buf.size()));
            log_it_(log_msg, log_enabled, traceback_enabled);
        }
        SPDLOG_LOGGER_CATCH(loc)
    }
#endif  // SPDLOG_WCHAR_TO_UTF8_SUPPORT

    /**
     * @brief 记录给定的消息（如果给定的日志级别足够高），并保存回溯（如果启用了回溯）
     * @param log_msg 日志消息对象
     * @param log_enabled 是否应该记录此消息
     * @param traceback_enabled 是否应该保存到回溯缓冲区
     * 
     * @details
     * 这是日志记录的核心方法，负责：
     * 1. 如果启用了日志记录，调用 sink_it_() 输出消息
     * 2. 如果启用了回溯，将消息保存到回溯缓冲区
     */
    void log_it_(const details::log_msg &log_msg, bool log_enabled, bool traceback_enabled);
    
    /**
     * @brief 将消息发送到所有 sink
     * @param msg 日志消息对象
     * 
     * @details
     * 遍历所有 sink，调用每个 sink 的 log() 方法。
     * 这是一个虚函数，子类可以重写以自定义行为。
     */
    virtual void sink_it_(const details::log_msg &msg);
    
    /**
     * @brief 刷新所有 sink 的缓冲区
     * 
     * @details
     * 遍历所有 sink，调用每个 sink 的 flush() 方法。
     * 这是一个虚函数，子类可以重写以自定义行为。
     */
    virtual void flush_();
    
    /**
     * @brief 转储回溯缓冲区的内部实现
     * 
     * @details
     * 将回溯缓冲区中的所有消息输出到此 logger。
     */
    void dump_backtrace_();
    
    /**
     * @brief 检查是否应该刷新缓冲区
     * @param msg 日志消息对象
     * @return 如果应该刷新，返回 true
     * 
     * @details
     * 检查消息的级别是否大于等于刷新级别。
     */
    bool should_flush_(const details::log_msg &msg) const;

    /**
     * @brief 处理日志记录期间的错误
     * @param msg 错误消息
     * 
     * @details
     * 默认处理器以最大 1 条消息/秒的速率将错误打印到 stderr。
     * 如果设置了自定义错误处理器，会调用自定义处理器。
     */
    void err_handler_(const std::string &msg) const;
};

/**
 * @brief 交换两个 logger 对象
 * @param a 第一个 logger
 * @param b 第二个 logger
 * 
 * @details
 * 高效地交换两个 logger 的所有成员变量。
 * 
 * @note 此函数不会抛出异常
 */
void swap(logger &a, logger &b) noexcept;

}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "logger-inl.h"
#endif
