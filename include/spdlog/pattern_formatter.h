// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file pattern_formatter.h
 * @brief 基于模式字符串的日志格式化器
 * 
 * @details
 * pattern_formatter 是 spdlog 中最常用的格式化器实现。
 * 它使用类似 printf 的模式字符串来定义日志的输出格式。
 * 
 * **核心功能**：
 * - 支持丰富的格式化标志（时间、级别、消息等）
 * - 支持自定义格式化标志
 * - 支持填充和对齐
 * - 支持本地时间和 UTC 时间
 * 
 * **常用格式化标志**：
 * - %v: 实际的日志消息
 * - %t: 线程 ID
 * - %P: 进程 ID
 * - %n: Logger 名称
 * - %l: 日志级别（如 info, debug）
 * - %L: 日志级别简写（如 I, D）
 * - %Y: 年份（4位）
 * - %m: 月份（01-12）
 * - %d: 日期（01-31）
 * - %H: 小时（00-23）
 * - %M: 分钟（00-59）
 * - %S: 秒（00-59）
 * - %e: 毫秒（000-999）
 * - %f: 微秒（000000-999999）
 * - %F: 纳秒（000000000-999999999）
 * - %%: 字面量 %
 * 
 * **颜色支持**：
 * - %^: 开始颜色范围
 * - %$: 结束颜色范围
 * 
 * @par 使用示例
 * @code
 * // 基本使用
 * auto logger = spdlog::stdout_color_mt("console");
 * logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
 * logger->info("Hello World");
 * // 输出: [2026-01-19 10:30:45.123] [info] Hello World
 * 
 * // 带线程 ID 和 logger 名称
 * logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%t] [%l] %v");
 * 
 * // 使用 UTC 时间
 * logger->set_formatter(std::make_unique<spdlog::pattern_formatter>(
 *     "[%Y-%m-%d %H:%M:%S.%e] %v", 
 *     spdlog::pattern_time_type::utc));
 * @endcode
 * 
 * @see formatter 格式化器基类
 */

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>

#include <chrono>
#include <ctime>
#include <memory>

#include <string>
#include <unordered_map>
#include <vector>

namespace spdlog {
namespace details {

/**
 * @struct padding_info
 * @brief 填充信息结构
 * 
 * @details
 * 定义如何对格式化的字段进行填充和对齐。
 * 填充可以让日志输出更加整齐美观。
 * 
 * **填充方向**：
 * - left: 左对齐，右侧填充空格
 * - right: 右对齐，左侧填充空格
 * - center: 居中对齐，两侧填充空格
 * 
 * @par 使用示例
 * @code
 * // 右对齐，宽度为 8
 * logger->set_pattern("[%8l] %v");
 * // 输出: [    info] message
 * 
 * // 左对齐，宽度为 8
 * logger->set_pattern("[%-8l] %v");
 * // 输出: [info    ] message
 * 
 * // 居中对齐，宽度为 8
 * logger->set_pattern("[%=8l] %v");
 * // 输出: [  info  ] message
 * @endcode
 */
struct padding_info {
    /**
     * @enum pad_side
     * @brief 填充方向枚举
     */
    enum class pad_side { 
        left,    ///< 左对齐（右侧填充）
        right,   ///< 右对齐（左侧填充）
        center   ///< 居中对齐（两侧填充）
    };

    /**
     * @brief 默认构造函数
     * 
     * @details
     * 创建一个禁用的填充信息（enabled_ = false）
     */
    padding_info() = default;
    
    /**
     * @brief 带参数的构造函数
     * 
     * @param width 填充宽度（字符数）
     * @param side 填充方向
     * @param truncate 如果内容超过宽度，是否截断
     */
    padding_info(size_t width, padding_info::pad_side side, bool truncate)
        : width_(width),
          side_(side),
          truncate_(truncate),
          enabled_(true) {}

    /**
     * @brief 检查填充是否启用
     * @return 如果启用返回 true
     */
    bool enabled() const { return enabled_; }
    
    size_t width_ = 0;              ///< 填充宽度
    pad_side side_ = pad_side::left; ///< 填充方向
    bool truncate_ = false;          ///< 是否截断超长内容
    bool enabled_ = false;           ///< 是否启用填充
};

/**
 * @class flag_formatter
 * @brief 单个格式化标志的处理器基类
 * 
 * @details
 * 每个格式化标志（如 %v, %l, %t）都有一个对应的 flag_formatter 实现。
 * 这是一个抽象基类，定义了标志格式化器的接口。
 * 
 * @note 这是内部实现类，通常不需要直接使用
 */
class SPDLOG_API flag_formatter {
public:
    /**
     * @brief 带填充信息的构造函数
     * @param padinfo 填充信息
     */
    explicit flag_formatter(padding_info padinfo)
        : padinfo_(padinfo) {}
        
    /**
     * @brief 默认构造函数
     */
    flag_formatter() = default;
    
    /**
     * @brief 虚析构函数
     */
    virtual ~flag_formatter() = default;
    
    /**
     * @brief 格式化标志（纯虚函数）
     * 
     * @param msg 日志消息
     * @param tm_time 格式化后的时间结构
     * @param dest 输出缓冲区
     */
    virtual void format(const details::log_msg &msg,
                        const std::tm &tm_time,
                        memory_buf_t &dest) = 0;

protected:
    padding_info padinfo_;  ///< 填充信息
};

}  // namespace details

/**
 * @class custom_flag_formatter
 * @brief 自定义格式化标志的基类
 * 
 * @details
 * 用户可以继承这个类来实现自定义的格式化标志。
 * 自定义标志可以添加到 pattern_formatter 中使用。
 * 
 * @par 使用示例
 * @code
 * // 定义一个自定义标志，输出固定文本
 * class my_flag_formatter : public spdlog::custom_flag_formatter {
 * public:
 *     void format(const spdlog::details::log_msg &,
 *                 const std::tm &,
 *                 spdlog::memory_buf_t &dest) override {
 *         std::string text = "MyApp";
 *         dest.append(text.data(), text.data() + text.size());
 *     }
 *     
 *     std::unique_ptr<custom_flag_formatter> clone() const override {
 *         return spdlog::details::make_unique<my_flag_formatter>();
 *     }
 * };
 * 
 * // 使用自定义标志
 * auto formatter = std::make_unique<spdlog::pattern_formatter>();
 * formatter->add_flag<my_flag_formatter>('*');
 * formatter->set_pattern("[%*] %v");  // %* 会输出 "MyApp"
 * 
 * auto logger = spdlog::stdout_color_mt("console");
 * logger->set_formatter(std::move(formatter));
 * @endcode
 */
class SPDLOG_API custom_flag_formatter : public details::flag_formatter {
public:
    /**
     * @brief 克隆自定义标志格式化器（纯虚函数）
     * @return 格式化器副本的唯一指针
     */
    virtual std::unique_ptr<custom_flag_formatter> clone() const = 0;

    /**
     * @brief 设置填充信息
     * @param padding 填充信息
     */
    void set_padding_info(const details::padding_info &padding) {
        flag_formatter::padinfo_ = padding;
    }
};

/**
 * @class pattern_formatter
 * @brief 基于模式字符串的格式化器实现
 * 
 * @details
 * pattern_formatter 是 formatter 接口的主要实现，
 * 使用模式字符串来定义日志的输出格式。
 * 
 * **核心特性**：
 * - 支持丰富的内置格式化标志
 * - 支持自定义格式化标志
 * - 支持字段填充和对齐
 * - 支持本地时间和 UTC 时间
 * - 高性能：缓存时间转换结果
 * 
 * **线程安全性**：
 * - 不是线程安全的
 * - 每个 logger 应该有自己的 pattern_formatter 实例
 * 
 * **性能优化**：
 * - 缓存上一秒的时间转换结果
 * - 预编译模式字符串为格式化器列表
 * - 避免重复的字符串操作
 * 
 * @note 这是一个 final 类，不能被继承
 */
class SPDLOG_API pattern_formatter final : public formatter {
public:
    /**
     * @typedef custom_flags
     * @brief 自定义标志映射类型
     * 
     * @details
     * 将字符映射到自定义格式化器的哈希表。
     * 键是标志字符（如 '*'），值是对应的格式化器。
     */
    using custom_flags = std::unordered_map<char, std::unique_ptr<custom_flag_formatter>>;

    /**
     * @brief 使用模式字符串构造格式化器
     * 
     * @details
     * 这是最常用的构造函数，允许指定完整的格式化配置。
     * 
     * @param pattern 模式字符串，定义日志格式
     * @param time_type 时间类型（本地时间或 UTC）
     * @param eol 行尾字符串（默认为系统默认值）
     * @param custom_user_flags 自定义格式化标志映射
     * 
     * @par 使用示例
     * @code
     * // 基本使用
     * auto formatter = std::make_unique<spdlog::pattern_formatter>(
     *     "[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
     * 
     * // 使用 UTC 时间
     * auto utc_formatter = std::make_unique<spdlog::pattern_formatter>(
     *     "[%Y-%m-%d %H:%M:%S.%e] [%l] %v",
     *     spdlog::pattern_time_type::utc);
     * 
     * // 自定义行尾
     * auto custom_eol_formatter = std::make_unique<spdlog::pattern_formatter>(
     *     "[%Y-%m-%d %H:%M:%S.%e] [%l] %v",
     *     spdlog::pattern_time_type::local,
     *     "\r\n");  // Windows 风格的行尾
     * @endcode
     */
    explicit pattern_formatter(std::string pattern,
                               pattern_time_type time_type = pattern_time_type::local,
                               std::string eol = spdlog::details::os::default_eol,
                               custom_flags custom_user_flags = custom_flags());

    /**
     * @brief 使用默认模式构造格式化器
     * 
     * @details
     * 如果不提供模式字符串，将使用默认模式。
     * 默认模式通常是: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v"
     * 
     * @param time_type 时间类型（本地时间或 UTC）
     * @param eol 行尾字符串
     * 
     * @par 使用示例
     * @code
     * // 使用默认模式和本地时间
     * auto formatter = std::make_unique<spdlog::pattern_formatter>();
     * 
     * // 使用默认模式和 UTC 时间
     * auto utc_formatter = std::make_unique<spdlog::pattern_formatter>(
     *     spdlog::pattern_time_type::utc);
     * @endcode
     */
    explicit pattern_formatter(pattern_time_type time_type = pattern_time_type::local,
                               std::string eol = spdlog::details::os::default_eol);

    /**
     * @brief 禁用拷贝构造函数
     * 
     * @details
     * pattern_formatter 包含不可拷贝的成员（如 unique_ptr），
     * 因此禁用拷贝构造。使用 clone() 方法来创建副本。
     */
    pattern_formatter(const pattern_formatter &other) = delete;
    
    /**
     * @brief 禁用拷贝赋值运算符
     */
    pattern_formatter &operator=(const pattern_formatter &other) = delete;

    /**
     * @brief 克隆格式化器
     * 
     * @details
     * 创建当前格式化器的完整副本，包括：
     * - 模式字符串
     * - 时间类型设置
     * - 行尾字符串
     * - 自定义标志
     * 
     * @return 格式化器副本的唯一指针
     * 
     * @note 实现了 formatter 接口的 clone() 方法
     */
    std::unique_ptr<formatter> clone() const override;
    
    /**
     * @brief 格式化日志消息
     * 
     * @details
     * 根据模式字符串格式化日志消息，将结果追加到输出缓冲区。
     * 
     * **性能优化**：
     * - 缓存上一秒的时间转换结果
     * - 只在秒数变化时重新转换时间
     * 
     * @param msg 要格式化的日志消息
     * @param dest 输出缓冲区
     * 
     * @note 实现了 formatter 接口的 format() 方法
     */
    void format(const details::log_msg &msg, memory_buf_t &dest) override;

    /**
     * @brief 添加自定义格式化标志
     * 
     * @details
     * 允许用户添加自定义的格式化标志到格式化器中。
     * 使用模板参数和完美转发来构造自定义格式化器。
     * 
     * @tparam T 自定义格式化器类型，必须继承自 custom_flag_formatter
     * @tparam Args 构造函数参数类型
     * @param flag 标志字符（如 '*', '#' 等）
     * @param args 传递给自定义格式化器构造函数的参数
     * @return 返回 *this，支持链式调用
     * 
     * @warning 不要使用已被内置标志占用的字符（如 v, l, t 等）
     * 
     * @par 使用示例
     * @code
     * class my_formatter : public spdlog::custom_flag_formatter {
     * public:
     *     void format(const spdlog::details::log_msg &,
     *                 const std::tm &,
     *                 spdlog::memory_buf_t &dest) override {
     *         std::string text = "custom";
     *         dest.append(text.data(), text.data() + text.size());
     *     }
     *     
     *     std::unique_ptr<custom_flag_formatter> clone() const override {
     *         return spdlog::details::make_unique<my_formatter>();
     *     }
     * };
     * 
     * auto formatter = std::make_unique<spdlog::pattern_formatter>();
     * formatter->add_flag<my_formatter>('*')
     *          .set_pattern("[%*] %v");  // 链式调用
     * @endcode
     */
    template <typename T, typename... Args>
    pattern_formatter &add_flag(char flag, Args &&...args) {
        custom_handlers_[flag] = details::make_unique<T>(std::forward<Args>(args)...);
        return *this;
    }
    
    /**
     * @brief 设置新的模式字符串
     * 
     * @details
     * 更改格式化器的模式字符串。
     * 这会重新编译模式，生成新的格式化器列表。
     * 
     * @param pattern 新的模式字符串
     * 
     * @par 使用示例
     * @code
     * auto formatter = std::make_unique<spdlog::pattern_formatter>();
     * 
     * // 初始模式
     * formatter->set_pattern("[%H:%M:%S] %v");
     * 
     * // 运行时更改模式
     * formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
     * @endcode
     */
    void set_pattern(std::string pattern);
    
    /**
     * @brief 设置是否需要本地时间
     * 
     * @details
     * 控制是否需要将时间戳转换为本地时间。
     * 如果设置为 false，将使用 UTC 时间。
     * 
     * @param need 是否需要本地时间，默认为 true
     * 
     * @note 这会影响所有时间相关的格式化标志
     */
    void need_localtime(bool need = true);

private:
    std::string pattern_;                                           ///< 模式字符串
    std::string eol_;                                               ///< 行尾字符串
    pattern_time_type pattern_time_type_;                           ///< 时间类型（本地/UTC）
    bool need_localtime_;                                           ///< 是否需要本地时间
    std::tm cached_tm_;                                             ///< 缓存的时间结构
    std::chrono::seconds last_log_secs_;                            ///< 上次日志的秒数（用于缓存）
    std::vector<std::unique_ptr<details::flag_formatter>> formatters_; ///< 编译后的格式化器列表
    custom_flags custom_handlers_;                                  ///< 自定义标志处理器

    /**
     * @brief 获取日志消息的时间结构
     * 
     * @details
     * 将日志消息的时间戳转换为 tm 结构。
     * 使用缓存优化：如果秒数未变化，直接返回缓存的结果。
     * 
     * @param msg 日志消息
     * @return 时间结构
     */
    std::tm get_time_(const details::log_msg &msg) const;
    
    /**
     * @brief 处理单个格式化标志
     * 
     * @details
     * 根据标志字符创建对应的格式化器，并添加到格式化器列表。
     * 
     * @tparam Padder 填充器类型
     * @param flag 标志字符
     * @param padding 填充信息
     */
    template <typename Padder>
    void handle_flag_(char flag, details::padding_info padding);

    /**
     * @brief 解析填充规范
     * 
     * @details
     * 从模式字符串中提取填充信息（如 %8l, %-8l, %=8l）。
     * 
     * @param it 当前迭代器位置（会被修改）
     * @param end 字符串结束位置
     * @return 解析出的填充信息
     */
    static details::padding_info handle_padspec_(std::string::const_iterator &it,
                                                 std::string::const_iterator end);

    /**
     * @brief 编译模式字符串
     * 
     * @details
     * 将模式字符串解析为格式化器列表。
     * 这是一个预处理步骤，提高运行时性能。
     * 
     * @param pattern 要编译的模式字符串
     */
    void compile_pattern_(const std::string &pattern);
};
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "pattern_formatter-inl.h"
#endif

