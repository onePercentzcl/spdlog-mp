// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file base_sink.h
 * @brief Sink 的基础实现模板类
 * 
 * @details
 * base_sink 是一个模板类，为具体的 sink 实现提供了通用的框架。
 * 它处理了线程同步、格式化器管理等通用逻辑，让派生类只需关注实际的输出操作。
 * 
 * **核心特性**：
 * - 模板化的互斥锁支持（可以是真实的锁或空锁）
 * - 自动处理线程同步
 * - 管理格式化器的生命周期
 * - 实现了 sink 接口的大部分方法
 * 
 * **设计模式**：
 * - 模板方法模式：定义算法框架，让子类实现具体步骤
 * - CRTP（奇异递归模板模式）：通过模板参数实现编译时多态
 * 
 * **线程安全性**：
 * - 通过模板参数 Mutex 控制线程安全性
 * - 使用 std::mutex 创建线程安全的 sink（_mt 后缀）
 * - 使用 details::null_mutex 创建单线程 sink（_st 后缀）
 * 
 * **派生类职责**：
 * 派生类只需要实现两个纯虚函数：
 * - sink_it_(): 实际的日志输出操作
 * - flush_(): 实际的刷新操作
 * 
 * @note 这是一个模板类，需要指定互斥锁类型
 * @see sink 基类接口
 * 
 * @par 使用示例
 * @code
 * // 创建一个简单的自定义 sink
 * template<typename Mutex>
 * class my_sink : public spdlog::sinks::base_sink<Mutex> {
 * protected:
 *     void sink_it_(const spdlog::details::log_msg& msg) override {
 *         // 格式化消息
 *         spdlog::memory_buf_t formatted;
 *         this->formatter_->format(msg, formatted);
 *         
 *         // 输出到自定义目标
 *         std::cout << std::string(formatted.data(), formatted.size());
 *     }
 *     
 *     void flush_() override {
 *         std::cout.flush();
 *     }
 * };
 * 
 * // 使用自定义 sink
 * using my_sink_mt = my_sink<std::mutex>;  // 多线程版本
 * using my_sink_st = my_sink<spdlog::details::null_mutex>;  // 单线程版本
 * 
 * auto logger = std::make_shared<spdlog::logger>(
 *     "my_logger", 
 *     std::make_shared<my_sink_mt>());
 * @endcode
 */

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/sink.h>

namespace spdlog {
namespace sinks {

/**
 * @class base_sink
 * @brief Sink 的基础实现模板类
 * 
 * @details
 * 这是一个模板类，通过模板参数 Mutex 来控制线程安全性。
 * 它实现了 sink 接口的大部分方法，并提供了线程同步机制。
 * 
 * **模板参数**：
 * - Mutex: 互斥锁类型
 *   - std::mutex: 用于多线程环境（_mt 后缀）
 *   - details::null_mutex: 用于单线程环境（_st 后缀）
 * 
 * **实现的功能**：
 * - 线程同步（通过 Mutex）
 * - 格式化器管理
 * - 日志级别过滤（继承自 sink）
 * 
 * **需要派生类实现的功能**：
 * - sink_it_(): 实际的日志输出
 * - flush_(): 实际的刷新操作
 * 
 * **性能考虑**：
 * - 使用 final 关键字优化虚函数调用
 * - 锁的粒度控制在单次日志操作
 * - 单线程版本使用空锁，零开销
 * 
 * @tparam Mutex 互斥锁类型，控制线程安全性
 * 
 * @note 这个类不能被直接实例化，必须通过派生类使用
 * @warning 派生类的 sink_it_() 和 flush_() 方法会在锁保护下被调用，不需要额外加锁
 */
template <typename Mutex>
class SPDLOG_API base_sink : public sink {
public:
    /**
     * @brief 默认构造函数
     * 
     * @details
     * 创建一个使用默认格式化器的 base_sink。
     * 默认格式化器通常是 pattern_formatter，使用默认模式。
     */
    base_sink();
    
    /**
     * @brief 使用指定格式化器构造
     * 
     * @details
     * 创建一个使用自定义格式化器的 base_sink。
     * 
     * @param formatter 格式化器的唯一指针（所有权转移）
     * 
     * @par 使用示例
     * @code
     * auto formatter = std::make_unique<spdlog::pattern_formatter>("[%H:%M:%S] %v");
     * auto sink = std::make_shared<my_sink_mt>(std::move(formatter));
     * @endcode
     */
    explicit base_sink(std::unique_ptr<spdlog::formatter> formatter);
    
    /**
     * @brief 虚析构函数
     * 
     * @details
     * 使用默认实现，确保派生类对象能够正确销毁。
     */
    ~base_sink() override = default;

    /**
     * @brief 禁用拷贝构造函数
     * 
     * @details
     * Sink 包含不可拷贝的资源（如互斥锁、格式化器），
     * 因此禁用拷贝构造。
     */
    base_sink(const base_sink &) = delete;
    
    /**
     * @brief 禁用移动构造函数
     * 
     * @details
     * 为了简化实现和避免潜在问题，禁用移动构造。
     */
    base_sink(base_sink &&) = delete;

    /**
     * @brief 禁用拷贝赋值运算符
     */
    base_sink &operator=(const base_sink &) = delete;
    
    /**
     * @brief 禁用移动赋值运算符
     */
    base_sink &operator=(base_sink &&) = delete;

    /**
     * @brief 记录日志消息（final 实现）
     * 
     * @details
     * 这是 sink 接口的实现，负责：
     * 1. 检查日志级别是否满足要求
     * 2. 加锁保护
     * 3. 调用派生类的 sink_it_() 方法
     * 4. 解锁
     * 
     * **线程安全性**：
     * - 使用 mutex_ 保护整个操作
     * - 派生类的 sink_it_() 在锁保护下执行
     * 
     * @param msg 要记录的日志消息
     * 
     * @note 使用 final 关键字，派生类不能重写此方法
     * @note 派生类应该重写 sink_it_() 而不是这个方法
     */
    void log(const details::log_msg &msg) final override;
    
    /**
     * @brief 刷新缓冲区（final 实现）
     * 
     * @details
     * 这是 sink 接口的实现，负责：
     * 1. 加锁保护
     * 2. 调用派生类的 flush_() 方法
     * 3. 解锁
     * 
     * **线程安全性**：
     * - 使用 mutex_ 保护整个操作
     * - 派生类的 flush_() 在锁保护下执行
     * 
     * @note 使用 final 关键字，派生类不能重写此方法
     * @note 派生类应该重写 flush_() 而不是这个方法
     */
    void flush() final override;
    
    /**
     * @brief 设置格式化模式（final 实现）
     * 
     * @details
     * 使用模式字符串设置格式化器。
     * 内部会创建一个新的 pattern_formatter。
     * 
     * **线程安全性**：
     * - 使用 mutex_ 保护格式化器的更新
     * 
     * @param pattern 模式字符串
     * 
     * @note 使用 final 关键字，派生类不能重写此方法
     * @note 派生类可以重写 set_pattern_() 来自定义行为
     */
    void set_pattern(const std::string &pattern) final override;
    
    /**
     * @brief 设置格式化器（final 实现）
     * 
     * @details
     * 直接设置格式化器对象。
     * 
     * **线程安全性**：
     * - 使用 mutex_ 保护格式化器的更新
     * 
     * @param sink_formatter 格式化器的唯一指针（所有权转移）
     * 
     * @note 使用 final 关键字，派生类不能重写此方法
     * @note 派生类可以重写 set_formatter_() 来自定义行为
     */
    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) final override;

protected:
    /**
     * @brief 格式化器
     * 
     * @details
     * 存储此 sink 使用的格式化器。
     * 派生类可以直接使用这个成员来格式化日志消息。
     * 
     * @note 这是一个 protected 成员，派生类可以访问
     */
    std::unique_ptr<spdlog::formatter> formatter_;
    
    /**
     * @brief 互斥锁
     * 
     * @details
     * 用于保护 sink 的线程安全性。
     * 类型由模板参数决定：
     * - std::mutex: 真实的互斥锁（多线程版本）
     * - details::null_mutex: 空锁，无开销（单线程版本）
     * 
     * @note 这是一个 protected 成员，派生类可以访问（但通常不需要）
     */
    Mutex mutex_;

    /**
     * @brief 实际的日志输出操作（纯虚函数）
     * 
     * @details
     * 派生类必须实现这个方法来执行实际的日志输出。
     * 
     * **实现要求**：
     * - 使用 formatter_ 格式化消息
     * - 将格式化后的消息输出到目标
     * - 处理可能的 I/O 错误
     * - 不需要加锁（已经在 log() 中加锁）
     * 
     * **调用时机**：
     * - 由 log() 方法在锁保护下调用
     * - 只有当消息级别满足要求时才会被调用
     * 
     * @param msg 要输出的日志消息
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @warning 不要在这个方法中加锁，已经在外层加锁了
     * 
     * @par 实现示例
     * @code
     * void my_sink::sink_it_(const spdlog::details::log_msg& msg) {
     *     // 格式化消息
     *     spdlog::memory_buf_t formatted;
     *     formatter_->format(msg, formatted);
     *     
     *     // 输出到文件
     *     file_.write(formatted.data(), formatted.size());
     * }
     * @endcode
     */
    virtual void sink_it_(const details::log_msg &msg) = 0;
    
    /**
     * @brief 实际的刷新操作（纯虚函数）
     * 
     * @details
     * 派生类必须实现这个方法来执行实际的刷新操作。
     * 
     * **实现要求**：
     * - 将所有缓冲的数据写入到输出目标
     * - 调用底层 I/O 的刷新操作
     * - 处理可能的 I/O 错误
     * - 不需要加锁（已经在 flush() 中加锁）
     * 
     * **调用时机**：
     * - 由 flush() 方法在锁保护下调用
     * 
     * @note 这是一个纯虚函数，派生类必须实现
     * @warning 不要在这个方法中加锁，已经在外层加锁了
     * 
     * @par 实现示例
     * @code
     * void my_sink::flush_() {
     *     // 刷新文件缓冲区
     *     if (file_.is_open()) {
     *         file_.flush();
     *     }
     * }
     * @endcode
     */
    virtual void flush_() = 0;
    
    /**
     * @brief 设置格式化模式（虚函数）
     * 
     * @details
     * 派生类可以重写这个方法来自定义模式设置行为。
     * 默认实现会创建一个新的 pattern_formatter。
     * 
     * @param pattern 模式字符串
     * 
     * @note 这个方法在锁保护下被调用
     * @note 大多数情况下不需要重写这个方法
     */
    virtual void set_pattern_(const std::string &pattern);
    
    /**
     * @brief 设置格式化器（虚函数）
     * 
     * @details
     * 派生类可以重写这个方法来自定义格式化器设置行为。
     * 默认实现会直接替换 formatter_ 成员。
     * 
     * @param sink_formatter 格式化器的唯一指针
     * 
     * @note 这个方法在锁保护下被调用
     * @note 大多数情况下不需要重写这个方法
     */
    virtual void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter);
};
}  // namespace sinks
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "base_sink-inl.h"
#endif

