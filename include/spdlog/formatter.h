// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file formatter.h
 * @brief 日志格式化器（Formatter）的抽象接口
 * 
 * @details
 * 格式化器负责将日志消息（log_msg）转换为最终的文本格式。
 * 这是一个抽象基类，定义了所有格式化器必须实现的接口。
 * 
 * **核心职责**：
 * - 将结构化的日志消息转换为格式化的文本
 * - 支持自定义的日志输出格式
 * - 提供克隆功能以支持 logger 的复制
 * 
 * **常见实现**：
 * - pattern_formatter: 基于模式字符串的格式化器（最常用）
 * - 自定义格式化器: 用户可以继承此类实现特殊格式
 * 
 * **设计模式**：
 * 使用策略模式（Strategy Pattern），允许在运行时更换格式化策略。
 * 
 * @note 这是一个纯虚类（接口），不能直接实例化
 * @see pattern_formatter 最常用的格式化器实现
 * 
 * @par 使用示例
 * @code
 * // 使用默认的 pattern_formatter
 * auto logger = spdlog::stdout_color_mt("console");
 * logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
 * 
 * // 创建自定义格式化器
 * class my_formatter : public spdlog::formatter {
 * public:
 *     void format(const spdlog::details::log_msg &msg, 
 *                 spdlog::memory_buf_t &dest) override {
 *         // 自定义格式化逻辑
 *         dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
 *     }
 *     
 *     std::unique_ptr<spdlog::formatter> clone() const override {
 *         return std::make_unique<my_formatter>();
 *     }
 * };
 * 
 * logger->set_formatter(std::make_unique<my_formatter>());
 * @endcode
 */

#include <spdlog/details/log_msg.h>
#include <spdlog/fmt/fmt.h>

namespace spdlog {

/**
 * @class formatter
 * @brief 日志格式化器的抽象基类
 * 
 * @details
 * 这是一个纯虚类，定义了格式化器的标准接口。
 * 所有的格式化器实现都必须继承这个类并实现其虚函数。
 * 
 * **接口设计**：
 * - format(): 执行实际的格式化操作
 * - clone(): 创建格式化器的副本
 * - 虚析构函数: 确保派生类对象能够正确销毁
 * 
 * **线程安全性**：
 * - 格式化器本身不保证线程安全
 * - 每个 logger 拥有自己的格式化器实例
 * - 如果多个线程共享同一个格式化器，需要外部同步
 * 
 * **性能考虑**：
 * - format() 方法会被频繁调用，应该尽可能高效
 * - 使用 memory_buf_t 避免不必要的内存分配
 * - 避免在 format() 中执行耗时操作
 * 
 * @note 实现自定义格式化器时，确保 format() 方法是线程安全的或者只在单线程环境使用
 */
class formatter {
public:
    /**
     * @brief 虚析构函数
     * 
     * @details
     * 使用虚析构函数确保通过基类指针删除派生类对象时，
     * 能够正确调用派生类的析构函数，避免资源泄漏。
     * 
     * @note 使用 = default 让编译器生成默认实现
     */
    virtual ~formatter() = default;
    
    /**
     * @brief 格式化日志消息（纯虚函数）
     * 
     * @details
     * 这是格式化器的核心方法，负责将结构化的日志消息转换为文本格式。
     * 
     * **实现要求**：
     * - 将格式化后的文本追加到 dest 缓冲区
     * - 不要清空 dest 的现有内容（使用追加而不是覆盖）
     * - 尽可能高效，避免不必要的内存分配
     * - 处理所有可能的日志消息字段（时间、级别、消息内容等）
     * 
     * **可用的消息字段**：
     * - msg.time: 日志时间戳
     * - msg.level: 日志级别
     * - msg.logger_name: logger 名称
     * - msg.payload: 日志消息内容
     * - msg.thread_id: 线程 ID
     * - msg.source: 源代码位置信息
     * 
     * @param msg 要格式化的日志消息，包含所有日志信息
     * @param dest 输出缓冲区，格式化后的文本应追加到此缓冲区
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @warning 此方法可能在高频率下被调用，必须保证高性能
     * 
     * @par 实现示例
     * @code
     * void my_formatter::format(const spdlog::details::log_msg &msg, 
     *                           spdlog::memory_buf_t &dest) {
     *     // 简单的格式: [级别] 消息内容
     *     dest.push_back('[');
     *     
     *     // 添加日志级别
     *     string_view_t level_name = spdlog::level::to_string_view(msg.level);
     *     dest.append(level_name.data(), level_name.data() + level_name.size());
     *     
     *     dest.append("] ", 2);
     *     
     *     // 添加消息内容
     *     dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
     * }
     * @endcode
     */
    virtual void format(const details::log_msg &msg, memory_buf_t &dest) = 0;
    
    /**
     * @brief 克隆格式化器（纯虚函数）
     * 
     * @details
     * 创建当前格式化器的一个副本。这个方法在以下场景中使用：
     * - 克隆 logger 时需要复制其格式化器
     * - 在多个 logger 之间共享相同的格式化配置
     * 
     * **实现要求**：
     * - 返回一个新的格式化器对象，具有相同的配置
     * - 使用 std::unique_ptr 管理内存，确保自动释放
     * - 新对象应该是完全独立的副本
     * 
     * @return 格式化器副本的唯一指针
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @note 返回 unique_ptr 而不是 shared_ptr，因为每个 logger 独占其格式化器
     * 
     * @par 实现示例
     * @code
     * std::unique_ptr<spdlog::formatter> my_formatter::clone() const {
     *     // 创建一个新的格式化器实例，复制当前配置
     *     return std::make_unique<my_formatter>(*this);
     * }
     * @endcode
     * 
     * @par 使用示例
     * @code
     * auto logger1 = spdlog::stdout_color_mt("logger1");
     * logger1->set_pattern("[%H:%M:%S] %v");
     * 
     * // 克隆 logger，格式化器也会被克隆
     * auto logger2 = logger1->clone("logger2");
     * 
     * // logger2 具有相同的格式化器配置
     * @endcode
     */
    virtual std::unique_ptr<formatter> clone() const = 0;
};
}  // namespace spdlog

