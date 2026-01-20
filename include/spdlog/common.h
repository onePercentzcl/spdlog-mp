/**
 * @file common.h
 * @brief spdlog 通用定义和类型声明
 * @author Gabi Melman 及 spdlog 贡献者
 * @copyright 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
 *            根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)
 * 
 * @details
 * 本文件包含 spdlog 库的核心类型定义、宏定义和通用工具。
 * 这是 spdlog 的基础头文件，被其他所有头文件所依赖。
 * 
 * 主要内容包括：
 * - 编译配置宏（API导出、内联等）
 * - 日志级别枚举和定义
 * - 通用类型别名（字符串视图、内存缓冲区等）
 * - 异常类定义
 * - 源代码位置信息结构
 * - 文件事件处理器
 */

#pragma once

#include <spdlog/details/null_mutex.h>
#include <spdlog/tweakme.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>

#ifdef SPDLOG_USE_STD_FORMAT
#include <version>
#if __cpp_lib_format >= 202207L
#include <format>
#else
#include <string_view>
#endif
#endif

#ifdef SPDLOG_COMPILED_LIB
#undef SPDLOG_HEADER_ONLY
#if defined(SPDLOG_SHARED_LIB)
#if defined(_WIN32)
#ifdef spdlog_EXPORTS
#define SPDLOG_API __declspec(dllexport)
#else  // !spdlog_EXPORTS
#define SPDLOG_API __declspec(dllimport)
#endif
#else  // !defined(_WIN32)
#define SPDLOG_API __attribute__((visibility("default")))
#endif
#else  // !defined(SPDLOG_SHARED_LIB)
#define SPDLOG_API
#endif
#define SPDLOG_INLINE
#else  // !defined(SPDLOG_COMPILED_LIB)
#define SPDLOG_API
#define SPDLOG_HEADER_ONLY
#define SPDLOG_INLINE inline
#endif  // #ifdef SPDLOG_COMPILED_LIB

#include <spdlog/fmt/fmt.h>

#if !defined(SPDLOG_USE_STD_FORMAT) && \
    FMT_VERSION >= 80000  // backward compatibility with fmt versions older than 8
#define SPDLOG_FMT_RUNTIME(format_string) fmt::runtime(format_string)
#define SPDLOG_FMT_STRING(format_string) FMT_STRING(format_string)
#if defined(SPDLOG_WCHAR_FILENAMES) || defined(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
#include <spdlog/fmt/xchar.h>
#endif
#else
#define SPDLOG_FMT_RUNTIME(format_string) format_string
#define SPDLOG_FMT_STRING(format_string) format_string
#endif

/**
 * @brief Visual Studio 2013 及更早版本不支持 noexcept 和 constexpr 关键字
 * @details 为了兼容旧版本编译器，这里定义了替代宏
 * - SPDLOG_NOEXCEPT: 在支持的编译器上展开为 noexcept，否则为 _NOEXCEPT
 * - SPDLOG_CONSTEXPR: 在支持的编译器上展开为 constexpr，否则为空
 */
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#define SPDLOG_NOEXCEPT _NOEXCEPT
#define SPDLOG_CONSTEXPR
#else
#define SPDLOG_NOEXCEPT noexcept
#define SPDLOG_CONSTEXPR constexpr
#endif

/**
 * @brief 如果使用 std::format 构建，可以直接使用 constexpr
 * @details 
 * 如果使用 fmt 库构建，SPDLOG_CONSTEXPR_FUNC 需要与 FMT_CONSTEXPR 保持一致，
 * 以避免 spdlog 中的 constexpr 函数调用 fmt 中的非 constexpr 函数的情况。
 * 如果 fmt 确定不能使用 constexpr，我们应该将函数内联。
 * 
 * @note 这是为了确保编译器兼容性和性能优化
 */
#ifdef SPDLOG_USE_STD_FORMAT
#define SPDLOG_CONSTEXPR_FUNC constexpr
#else  // 使用 fmt 库构建
#if FMT_USE_CONSTEXPR
#define SPDLOG_CONSTEXPR_FUNC FMT_CONSTEXPR
#else
#define SPDLOG_CONSTEXPR_FUNC inline
#endif
#endif

/**
 * @brief 标记已弃用的函数或类
 * @details 
 * 根据不同的编译器定义相应的弃用属性：
 * - GCC/Clang: 使用 __attribute__((deprecated))
 * - MSVC: 使用 __declspec(deprecated)
 * - 其他编译器: 定义为空
 * 
 * @note 使用此宏标记的函数在调用时会产生编译警告
 */
#if defined(__GNUC__) || defined(__clang__)
#define SPDLOG_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define SPDLOG_DEPRECATED __declspec(deprecated)
#else
#define SPDLOG_DEPRECATED
#endif

/**
 * @brief 在 MSVC 2013 上禁用线程局部存储
 * @details 
 * MSVC 2013 和 Windows Runtime 不支持线程局部存储（Thread Local Storage, TLS）。
 * 如果检测到这些环境，将 SPDLOG_NO_TLS 定义为 1。
 * 
 * @note 线程局部存储用于存储每个线程独立的数据，如 MDC（Mapped Diagnostic Context）
 */
#ifndef SPDLOG_NO_TLS
#if (defined(_MSC_VER) && (_MSC_VER < 1900)) || defined(__cplusplus_winrt)
#define SPDLOG_NO_TLS 1
#endif
#endif

/**
 * @brief 获取当前函数名的宏
 * @details 
 * 使用编译器提供的 __FUNCTION__ 宏来获取当前函数名。
 * 这在日志记录中用于显示日志消息来自哪个函数。
 * 
 * @note 不同编译器可能使用不同的宏名（如 __func__, __FUNCTION__ 等）
 */
#ifndef SPDLOG_FUNCTION
#define SPDLOG_FUNCTION static_cast<const char *>(__FUNCTION__)
#endif

/**
 * @brief 异常处理相关的宏定义
 * @details 
 * 如果定义了 SPDLOG_NO_EXCEPTIONS，则禁用异常处理：
 * - SPDLOG_TRY: 展开为空（不使用 try）
 * - SPDLOG_THROW: 打印错误信息并中止程序
 * - SPDLOG_CATCH_STD: 展开为空（不捕获异常）
 * 
 * 如果启用异常处理：
 * - SPDLOG_TRY: 展开为 try
 * - SPDLOG_THROW: 展开为 throw
 * - SPDLOG_CATCH_STD: 捕获 std::exception 但不做任何处理
 * 
 * @note 在某些嵌入式系统或特殊环境中可能需要禁用异常
 */
#ifdef SPDLOG_NO_EXCEPTIONS
#define SPDLOG_TRY
#define SPDLOG_THROW(ex)                               \
    do {                                               \
        printf("spdlog fatal error: %s\n", ex.what()); \
        std::abort();                                  \
    } while (0)
#define SPDLOG_CATCH_STD
#else
#define SPDLOG_TRY try
#define SPDLOG_THROW(ex) throw(ex)
#define SPDLOG_CATCH_STD             \
    catch (const std::exception &) { \
    }
#endif

namespace spdlog {

// 前向声明：格式化器类
class formatter;

// 前向声明：sink 命名空间
namespace sinks {
class sink;
}

/**
 * @brief 文件名类型定义
 * @details 
 * 在 Windows 平台且定义了 SPDLOG_WCHAR_FILENAMES 时，使用宽字符串（std::wstring）。
 * 这是为了支持 Windows 上的 Unicode 文件名。
 * 在其他平台上使用普通字符串（std::string）。
 * 
 * @note 宽字符串可以正确处理包含非 ASCII 字符的文件名
 */
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
using filename_t = std::wstring;
// 允许宏展开发生在 SPDLOG_FILENAME_T 中
#define SPDLOG_FILENAME_T_INNER(s) L##s
#define SPDLOG_FILENAME_T(s) SPDLOG_FILENAME_T_INNER(s)
#else
using filename_t = std::string;
#define SPDLOG_FILENAME_T(s) s
#endif

/**
 * @typedef log_clock
 * @brief 日志时钟类型
 * @details 使用系统时钟（std::chrono::system_clock）来获取日志时间戳
 */
using log_clock = std::chrono::system_clock;

/**
 * @typedef sink_ptr
 * @brief Sink 的智能指针类型
 * @details 使用 shared_ptr 管理 sink 对象的生命周期，允许多个 logger 共享同一个 sink
 */
using sink_ptr = std::shared_ptr<sinks::sink>;

/**
 * @typedef sinks_init_list
 * @brief Sink 初始化列表类型
 * @details 用于在构造 logger 时传递多个 sink
 */
using sinks_init_list = std::initializer_list<sink_ptr>;

/**
 * @typedef err_handler
 * @brief 错误处理器类型
 * @details 
 * 错误处理器是一个函数对象，接受错误消息字符串作为参数。
 * 当 spdlog 内部发生错误时（如写入文件失败），会调用此处理器。
 * 
 * @param err_msg 错误消息字符串
 */
using err_handler = std::function<void(const std::string &err_msg)>;
#ifdef SPDLOG_USE_STD_FORMAT
namespace fmt_lib = std;

using string_view_t = std::string_view;
using memory_buf_t = std::string;

template <typename... Args>
#if __cpp_lib_format >= 202207L
using format_string_t = std::format_string<Args...>;
#else
using format_string_t = std::string_view;
#endif

template <class T, class Char = char>
struct is_convertible_to_basic_format_string
    : std::integral_constant<bool, std::is_convertible<T, std::basic_string_view<Char>>::value> {};

#if defined(SPDLOG_WCHAR_FILENAMES) || defined(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
using wstring_view_t = std::wstring_view;
using wmemory_buf_t = std::wstring;

template <typename... Args>
#if __cpp_lib_format >= 202207L
using wformat_string_t = std::wformat_string<Args...>;
#else
using wformat_string_t = std::wstring_view;
#endif
#endif
#define SPDLOG_BUF_TO_STRING(x) x
#else  // use fmt lib instead of std::format
namespace fmt_lib = fmt;

using string_view_t = fmt::basic_string_view<char>;
using memory_buf_t = fmt::basic_memory_buffer<char, 250>;

template <typename... Args>
using format_string_t = fmt::format_string<Args...>;

template <class T>
using remove_cvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename Char>
#if FMT_VERSION >= 90101
using fmt_runtime_string = fmt::runtime_format_string<Char>;
#else
using fmt_runtime_string = fmt::basic_runtime<Char>;
#endif

// clang doesn't like SFINAE disabled constructor in std::is_convertible<> so have to repeat the
// condition from basic_format_string here, in addition, fmt::basic_runtime<Char> is only
// convertible to basic_format_string<Char> but not basic_string_view<Char>
template <class T, class Char = char>
struct is_convertible_to_basic_format_string
    : std::integral_constant<bool,
                             std::is_convertible<T, fmt::basic_string_view<Char>>::value ||
                                 std::is_same<remove_cvref_t<T>, fmt_runtime_string<Char>>::value> {
};

#if defined(SPDLOG_WCHAR_FILENAMES) || defined(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
using wstring_view_t = fmt::basic_string_view<wchar_t>;
using wmemory_buf_t = fmt::basic_memory_buffer<wchar_t, 250>;

template <typename... Args>
using wformat_string_t = fmt::wformat_string<Args...>;
#endif
#define SPDLOG_BUF_TO_STRING(x) fmt::to_string(x)
#endif

#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT
#ifndef _WIN32
#error SPDLOG_WCHAR_TO_UTF8_SUPPORT only supported on windows
#endif  // _WIN32
#endif  // SPDLOG_WCHAR_TO_UTF8_SUPPORT

template <class T>
struct is_convertible_to_any_format_string
    : std::integral_constant<bool,
                             is_convertible_to_basic_format_string<T, char>::value ||
                                 is_convertible_to_basic_format_string<T, wchar_t>::value> {};

/**
 * @brief 日志级别类型定义
 * @details 
 * 根据是否定义 SPDLOG_NO_ATOMIC_LEVELS 来选择：
 * - 如果定义了，使用非原子整数（适用于单线程环境）
 * - 如果未定义，使用原子整数（适用于多线程环境，默认）
 * 
 * @note 原子操作确保多线程环境下的线程安全，但有轻微的性能开销
 */
#if defined(SPDLOG_NO_ATOMIC_LEVELS)
using level_t = details::null_atomic_int;
#else
using level_t = std::atomic<int>;
#endif

/**
 * @name 日志级别常量定义
 * @brief 定义各个日志级别对应的整数值
 * @details 
 * 日志级别从低到高依次为：
 * - TRACE (0): 最详细的调试信息
 * - DEBUG (1): 调试信息
 * - INFO (2): 一般信息
 * - WARN (3): 警告信息
 * - ERROR (4): 错误信息
 * - CRITICAL (5): 严重错误
 * - OFF (6): 关闭日志
 * 
 * @note 数值越大，级别越高。只有级别大于等于 logger 设置级别的消息才会被记录
 * @{
 */
#define SPDLOG_LEVEL_TRACE 0
#define SPDLOG_LEVEL_DEBUG 1
#define SPDLOG_LEVEL_INFO 2
#define SPDLOG_LEVEL_WARN 3
#define SPDLOG_LEVEL_ERROR 4
#define SPDLOG_LEVEL_CRITICAL 5
#define SPDLOG_LEVEL_OFF 6
/** @} */

/**
 * @brief 活动日志级别定义
 * @details 
 * SPDLOG_ACTIVE_LEVEL 控制编译时的日志级别。
 * 低于此级别的日志调用会在编译时被完全移除，不会产生任何运行时开销。
 * 
 * 默认值为 SPDLOG_LEVEL_INFO，意味着 TRACE 和 DEBUG 级别的日志会被编译移除。
 * 
 * @note 要启用所有级别的日志，在包含 spdlog.h 之前定义：
 *       #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
 */
#if !defined(SPDLOG_ACTIVE_LEVEL)
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

/**
 * @brief 日志级别枚举和相关函数
 * @details 此命名空间包含日志级别的枚举定义和转换函数
 */
namespace level {

/**
 * @enum level_enum
 * @brief 日志级别枚举
 * @details 
 * 定义了 spdlog 支持的所有日志级别。
 * 每个级别对应一个整数值，用于比较和过滤日志消息。
 * 
 * 使用示例：
 * @code
 * logger->set_level(spdlog::level::debug);  // 设置日志级别为 debug
 * logger->info("这是一条 info 级别的日志");  // 会被记录
 * logger->trace("这是一条 trace 级别的日志"); // 不会被记录（低于 debug）
 * @endcode
 */
enum level_enum : int {
    trace = SPDLOG_LEVEL_TRACE,       ///< 跟踪级别：最详细的调试信息，用于追踪程序执行流程
    debug = SPDLOG_LEVEL_DEBUG,       ///< 调试级别：调试信息，用于开发和调试阶段
    info = SPDLOG_LEVEL_INFO,         ///< 信息级别：一般信息，用于记录程序正常运行的重要事件
    warn = SPDLOG_LEVEL_WARN,         ///< 警告级别：警告信息，表示潜在问题但不影响程序运行
    err = SPDLOG_LEVEL_ERROR,         ///< 错误级别：错误信息，表示发生了错误但程序可以继续运行
    critical = SPDLOG_LEVEL_CRITICAL, ///< 严重级别：严重错误，表示发生了严重问题，可能导致程序崩溃
    off = SPDLOG_LEVEL_OFF,           ///< 关闭级别：关闭所有日志输出
    n_levels                          ///< 级别数量：用于内部数组大小计算
};

/**
 * @name 日志级别名称定义
 * @brief 定义各个日志级别的字符串表示
 * @details 这些宏定义了日志级别的完整名称和简短名称
 * @{
 */
#define SPDLOG_LEVEL_NAME_TRACE spdlog::string_view_t("trace", 5)
#define SPDLOG_LEVEL_NAME_DEBUG spdlog::string_view_t("debug", 5)
#define SPDLOG_LEVEL_NAME_INFO spdlog::string_view_t("info", 4)
#define SPDLOG_LEVEL_NAME_WARNING spdlog::string_view_t("warning", 7)
#define SPDLOG_LEVEL_NAME_ERROR spdlog::string_view_t("error", 5)
#define SPDLOG_LEVEL_NAME_CRITICAL spdlog::string_view_t("critical", 8)
#define SPDLOG_LEVEL_NAME_OFF spdlog::string_view_t("off", 3)

/**
 * @brief 日志级别完整名称数组
 * @details 包含所有日志级别的完整字符串表示，用于格式化输出
 */
#if !defined(SPDLOG_LEVEL_NAMES)
#define SPDLOG_LEVEL_NAMES                                                                  \
    {                                                                                       \
        SPDLOG_LEVEL_NAME_TRACE, SPDLOG_LEVEL_NAME_DEBUG, SPDLOG_LEVEL_NAME_INFO,           \
            SPDLOG_LEVEL_NAME_WARNING, SPDLOG_LEVEL_NAME_ERROR, SPDLOG_LEVEL_NAME_CRITICAL, \
            SPDLOG_LEVEL_NAME_OFF                                                           \
    }
#endif

/**
 * @brief 日志级别简短名称数组
 * @details 
 * 包含所有日志级别的单字符表示，用于紧凑的日志输出：
 * - T: Trace
 * - D: Debug
 * - I: Info
 * - W: Warning
 * - E: Error
 * - C: Critical
 * - O: Off
 */
#if !defined(SPDLOG_SHORT_LEVEL_NAMES)
#define SPDLOG_SHORT_LEVEL_NAMES \
    { "T", "D", "I", "W", "E", "C", "O" }
#endif
/** @} */

/**
 * @brief 将日志级别枚举转换为字符串视图
 * @param l 日志级别枚举值
 * @return 对应的字符串视图（如 "info", "debug" 等）
 * @note 此函数不会抛出异常
 */
SPDLOG_API const string_view_t &to_string_view(spdlog::level::level_enum l) SPDLOG_NOEXCEPT;

/**
 * @brief 将日志级别枚举转换为简短的 C 字符串
 * @param l 日志级别枚举值
 * @return 对应的单字符 C 字符串（如 "I", "D" 等）
 * @note 此函数不会抛出异常
 */
SPDLOG_API const char *to_short_c_str(spdlog::level::level_enum l) SPDLOG_NOEXCEPT;

/**
 * @brief 从字符串解析日志级别
 * @param name 日志级别的字符串表示（如 "info", "debug" 等）
 * @return 对应的日志级别枚举值，如果无法识别则返回 off
 * @note 此函数不会抛出异常，不区分大小写
 */
SPDLOG_API spdlog::level::level_enum from_str(const std::string &name) SPDLOG_NOEXCEPT;

}  // namespace level

/**
 * @enum color_mode
 * @brief 颜色模式枚举
 * @details 
 * 控制具有颜色支持的 sink（如控制台输出）如何显示颜色：
 * - always: 总是使用颜色
 * - automatic: 自动检测（如果输出到终端则使用颜色，否则不使用）
 * - never: 从不使用颜色
 * 
 * @note 颜色输出可以提高日志的可读性，但在某些环境下可能不被支持
 */
enum class color_mode { 
    always,     ///< 总是使用颜色输出
    automatic,  ///< 自动检测是否支持颜色（默认）
    never       ///< 从不使用颜色输出
};

/**
 * @enum pattern_time_type
 * @brief 模式时间类型枚举
 * @details 
 * 指定 pattern_formatter 使用哪种时间：
 * - local: 使用本地时间（考虑时区）
 * - utc: 使用 UTC 时间（协调世界时）
 * 
 * 默认使用本地时间。
 * 
 * @note UTC 时间在分布式系统中很有用，可以避免时区混淆
 */
enum class pattern_time_type {
    local,  ///< 记录本地时间（默认）
    utc     ///< 记录 UTC 时间
};

/**
 * @class spdlog_ex
 * @brief spdlog 异常类
 * @details 
 * 这是 spdlog 库抛出的所有异常的基类。
 * 继承自 std::exception，可以使用标准的异常处理机制捕获。
 * 
 * 使用示例：
 * @code
 * try {
 *     auto logger = spdlog::get("non_existent");
 * } catch (const spdlog::spdlog_ex& ex) {
 *     std::cerr << "日志错误: " << ex.what() << std::endl;
 * }
 * @endcode
 */
class SPDLOG_API spdlog_ex : public std::exception {
public:
    /**
     * @brief 构造函数
     * @param msg 错误消息
     */
    explicit spdlog_ex(std::string msg);
    
    /**
     * @brief 构造函数（包含系统错误码）
     * @param msg 错误消息
     * @param last_errno 系统错误码（如 errno 的值）
     */
    spdlog_ex(const std::string &msg, int last_errno);
    
    /**
     * @brief 获取异常描述
     * @return 错误消息的 C 字符串
     */
    const char *what() const SPDLOG_NOEXCEPT override;

private:
    std::string msg_;  ///< 错误消息
};

/**
 * @brief 抛出 spdlog 异常（包含系统错误码）
 * @param msg 错误消息
 * @param last_errno 系统错误码
 * @note 此函数不会返回（标记为 [[noreturn]]）
 */
[[noreturn]] SPDLOG_API void throw_spdlog_ex(const std::string &msg, int last_errno);

/**
 * @brief 抛出 spdlog 异常
 * @param msg 错误消息
 * @note 此函数不会返回（标记为 [[noreturn]]）
 */
[[noreturn]] SPDLOG_API void throw_spdlog_ex(std::string msg);

/**
 * @struct source_loc
 * @brief 源代码位置信息结构
 * @details 
 * 记录日志消息在源代码中的位置，包括文件名、行号和函数名。
 * 这些信息在调试时非常有用，可以快速定位日志消息的来源。
 * 
 * 使用示例：
 * @code
 * spdlog::source_loc loc{__FILE__, __LINE__, __FUNCTION__};
 * logger->log(loc, spdlog::level::info, "带位置信息的日志");
 * @endcode
 * 
 * @note 通常不需要手动创建，spdlog 的宏会自动填充这些信息
 */
struct source_loc {
    /**
     * @brief 默认构造函数
     * @details 创建一个空的源位置对象（所有字段为默认值）
     */
    SPDLOG_CONSTEXPR source_loc() = default;
    
    /**
     * @brief 构造函数
     * @param filename_in 源文件名（通常来自 __FILE__ 宏）
     * @param line_in 行号（通常来自 __LINE__ 宏）
     * @param funcname_in 函数名（通常来自 __FUNCTION__ 宏）
     */
    SPDLOG_CONSTEXPR source_loc(const char *filename_in, int line_in, const char *funcname_in)
        : filename{filename_in},
          line{line_in},
          funcname{funcname_in} {}

    /**
     * @brief 检查源位置是否为空
     * @return 如果行号小于等于 0，返回 true（表示无效的源位置）
     */
    SPDLOG_CONSTEXPR bool empty() const SPDLOG_NOEXCEPT { return line <= 0; }
    
    const char *filename{nullptr};  ///< 源文件名
    int line{0};                    ///< 行号
    const char *funcname{nullptr};  ///< 函数名
};

/**
 * @struct file_event_handlers
 * @brief 文件事件处理器结构
 * @details 
 * 允许用户在文件操作的关键时刻注册回调函数。
 * 这对于实现自定义的文件管理逻辑很有用，例如：
 * - 在打开文件前检查权限
 * - 在打开文件后设置文件属性
 * - 在关闭文件前刷新缓冲区
 * - 在关闭文件后进行清理工作
 * 
 * 使用示例：
 * @code
 * spdlog::file_event_handlers handlers;
 * handlers.before_open = [](const spdlog::filename_t& filename) {
 *     std::cout << "即将打开文件: " << filename << std::endl;
 * };
 * handlers.after_open = [](const spdlog::filename_t& filename, std::FILE* file) {
 *     std::cout << "文件已打开: " << filename << std::endl;
 * };
 * // 将 handlers 传递给文件 sink
 * @endcode
 */
struct file_event_handlers {
    /**
     * @brief 默认构造函数
     * @details 将所有处理器初始化为 nullptr（不设置任何回调）
     */
    file_event_handlers()
        : before_open(nullptr),
          after_open(nullptr),
          before_close(nullptr),
          after_close(nullptr) {}

    /**
     * @brief 文件打开前的回调函数
     * @details 在尝试打开文件之前调用，可用于权限检查或准备工作
     */
    std::function<void(const filename_t &filename)> before_open;
    
    /**
     * @brief 文件打开后的回调函数
     * @details 在成功打开文件之后调用，可用于设置文件属性或记录日志
     */
    std::function<void(const filename_t &filename, std::FILE *file_stream)> after_open;
    
    /**
     * @brief 文件关闭前的回调函数
     * @details 在关闭文件之前调用，可用于刷新缓冲区或保存状态
     */
    std::function<void(const filename_t &filename, std::FILE *file_stream)> before_close;
    
    /**
     * @brief 文件关闭后的回调函数
     * @details 在关闭文件之后调用，可用于清理工作或记录日志
     */
    std::function<void(const filename_t &filename)> after_close;
};

/**
 * @namespace details
 * @brief spdlog 内部实现细节命名空间
 * @details 
 * 此命名空间包含 spdlog 的内部实现细节，不应该被用户代码直接使用。
 * 这些函数和类型主要用于 spdlog 内部的实现。
 * 
 * @warning 此命名空间中的 API 可能在版本更新时发生变化，不保证向后兼容性
 */
namespace details {

/**
 * @name 字符串视图转换函数
 * @brief 将各种类型转换为字符串视图
 * @details 
 * 字符串视图（string_view）是一个轻量级的字符串引用，不拥有字符串数据。
 * 这些函数用于在不同的字符串类型之间进行高效转换。
 * @{
 */

/**
 * @brief 将内存缓冲区转换为字符串视图
 * @param buf 内存缓冲区
 * @return 指向缓冲区数据的字符串视图
 * @note 此函数不会抛出异常
 */
SPDLOG_CONSTEXPR_FUNC spdlog::string_view_t to_string_view(const memory_buf_t &buf)
    SPDLOG_NOEXCEPT {
    return spdlog::string_view_t{buf.data(), buf.size()};
}

/**
 * @brief 字符串视图的恒等转换
 * @param str 字符串视图
 * @return 相同的字符串视图
 * @note 此函数不会抛出异常
 */
SPDLOG_CONSTEXPR_FUNC spdlog::string_view_t to_string_view(spdlog::string_view_t str)
    SPDLOG_NOEXCEPT {
    return str;
}

#if defined(SPDLOG_WCHAR_FILENAMES) || defined(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
/**
 * @brief 将宽字符内存缓冲区转换为宽字符串视图
 * @param buf 宽字符内存缓冲区
 * @return 指向缓冲区数据的宽字符串视图
 * @note 此函数不会抛出异常
 */
SPDLOG_CONSTEXPR_FUNC spdlog::wstring_view_t to_string_view(const wmemory_buf_t &buf)
    SPDLOG_NOEXCEPT {
    return spdlog::wstring_view_t{buf.data(), buf.size()};
}

/**
 * @brief 宽字符串视图的恒等转换
 * @param str 宽字符串视图
 * @return 相同的宽字符串视图
 * @note 此函数不会抛出异常
 */
SPDLOG_CONSTEXPR_FUNC spdlog::wstring_view_t to_string_view(spdlog::wstring_view_t str)
    SPDLOG_NOEXCEPT {
    return str;
}
#endif

#if defined(SPDLOG_USE_STD_FORMAT) && __cpp_lib_format >= 202207L
/**
 * @brief 从 std::format_string 提取字符串视图
 * @tparam T 字符类型
 * @tparam Args 格式化参数类型
 * @param fmt 格式化字符串对象
 * @return 格式化字符串的视图
 * @note 此函数不会抛出异常
 */
template <typename T, typename... Args>
SPDLOG_CONSTEXPR_FUNC std::basic_string_view<T> to_string_view(
    std::basic_format_string<T, Args...> fmt) SPDLOG_NOEXCEPT {
    return fmt.get();
}
#endif
/** @} */

/**
 * @name C++14 之前的兼容性支持
 * @brief 为 C++11 提供 C++14 标准库功能
 * @details 
 * 如果编译器支持 C++14 或更高版本，直接使用标准库的实现。
 * 否则，提供自定义实现以保持兼容性。
 * @{
 */
#if __cplusplus >= 201402L  // C++14 及更高版本
using std::enable_if_t;
using std::make_unique;
#else
/**
 * @brief enable_if 的类型别名（C++14 特性）
 * @tparam B 布尔条件
 * @tparam T 类型（默认为 void）
 */
template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

/**
 * @brief 创建 unique_ptr 的工厂函数（C++14 特性）
 * @tparam T 要创建的对象类型
 * @tparam Args 构造函数参数类型
 * @param args 构造函数参数
 * @return 指向新创建对象的 unique_ptr
 * @note 不支持数组类型
 */
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&...args) {
    static_assert(!std::is_array<T>::value, "arrays not supported");
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif
/** @} */

/**
 * @name 条件类型转换
 * @brief 避免不必要的类型转换
 * @details 
 * 这些函数模板根据源类型和目标类型是否相同来决定是否进行类型转换。
 * 如果类型相同，直接返回值；如果类型不同，进行 static_cast 转换。
 * 这可以避免编译器警告并提高代码清晰度。
 * 
 * @see https://github.com/nlohmann/json/issues/2893#issuecomment-889152324
 * @{
 */

/**
 * @brief 条件类型转换（类型不同时）
 * @tparam T 目标类型
 * @tparam U 源类型
 * @param value 要转换的值
 * @return 转换后的值
 */
template <typename T, typename U, enable_if_t<!std::is_same<T, U>::value, int> = 0>
constexpr T conditional_static_cast(U value) {
    return static_cast<T>(value);
}

/**
 * @brief 条件类型转换（类型相同时）
 * @tparam T 目标类型
 * @tparam U 源类型（与 T 相同）
 * @param value 要转换的值
 * @return 原值（不进行转换）
 */
template <typename T, typename U, enable_if_t<std::is_same<T, U>::value, int> = 0>
constexpr T conditional_static_cast(U value) {
    return value;
}
/** @} */

}  // namespace details
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "common-inl.h"
#endif
