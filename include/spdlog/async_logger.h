// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file async_logger.h
 * @brief 异步日志记录器（Async Logger）的实现
 * 
 * @details
 * 异步日志记录器是一种高性能的日志记录方式，它将日志消息的生成和实际写入操作分离：
 * 
 * **工作原理**：
 * - 使用预分配的消息队列来缓存日志消息
 * - 创建一个后台线程从队列中取出消息并执行实际的日志写入
 * - 前台线程只需要将消息放入队列，不会被 I/O 操作阻塞
 * 
 * **日志记录流程**：
 * 每次调用日志函数时，logger 会执行以下步骤：
 * 1. 检查日志级别是否足够记录该消息
 * 2. 将消息的副本推送到队列（如果队列满了，根据溢出策略决定是阻塞还是丢弃）
 * 3. 立即返回，不等待实际写入完成
 * 
 * **生命周期管理**：
 * - 在 logger 销毁时，会等待队列中的所有剩余消息都被处理完毕
 * - 确保不会丢失已经提交到队列的日志消息
 * 
 * @note 异步日志适用于高频率日志记录场景，可以显著减少日志记录对主线程的影响
 * @warning 异步日志会增加内存使用（队列缓存）和轻微的延迟（消息在队列中等待）
 * 
 * @par 使用示例
 * @code
 * // 创建异步 logger
 * auto async_file = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("async_logger", "logs/async.log");
 * async_file->info("这条消息会被异步写入");
 * @endcode
 */

#include <spdlog/logger.h>

namespace spdlog {

/**
 * @enum async_overflow_policy
 * @brief 异步队列溢出策略
 * 
 * @details
 * 当异步日志的消息队列满时，定义如何处理新的日志消息。
 * 不同的策略适用于不同的应用场景：
 * 
 * - block: 适用于不能丢失任何日志的场景（如金融系统）
 * - overrun_oldest: 适用于希望保留最新日志的场景（如实时监控）
 * - discard_new: 适用于希望保留历史日志的场景（如故障分析）
 * 
 * @note 默认策略是 block，这是最安全的选择
 */
enum class async_overflow_policy {
    /**
     * @brief 阻塞模式（默认）
     * 
     * @details
     * 当队列满时，阻塞调用线程，直到队列中有空间可以放入新消息。
     * 
     * **特点**：
     * - 保证不会丢失任何日志消息
     * - 可能会阻塞业务线程（如果日志写入速度跟不上生成速度）
     * - 适用于日志完整性要求高的场景
     * 
     * @warning 如果后台线程处理速度过慢，可能导致前台线程长时间阻塞
     */
    block,
    
    /**
     * @brief 覆盖最旧消息模式
     * 
     * @details
     * 当队列满时，丢弃队列中最旧的消息，为新消息腾出空间。
     * 
     * **特点**：
     * - 永远不会阻塞调用线程
     * - 保留最新的日志消息
     * - 可能会丢失历史日志
     * - 适用于实时监控、调试等场景
     * 
     * @warning 在高负载情况下可能会丢失大量历史日志
     */
    overrun_oldest,
    
    /**
     * @brief 丢弃新消息模式
     * 
     * @details
     * 当队列满时，直接丢弃新的日志消息，保留队列中已有的消息。
     * 
     * **特点**：
     * - 永远不会阻塞调用线程
     * - 保留历史日志消息
     * - 可能会丢失最新的日志
     * - 适用于故障分析、审计等场景
     * 
     * @warning 在高负载情况下可能会丢失大量最新日志
     */
    discard_new
};

namespace details {
class thread_pool;
}

/**
 * @class async_logger
 * @brief 异步日志记录器类
 * 
 * @details
 * async_logger 继承自 logger 类，提供异步日志记录功能。
 * 它使用线程池（thread_pool）来处理日志消息的实际写入操作。
 * 
 * **核心特性**：
 * - 非阻塞日志记录（取决于溢出策略）
 * - 使用预分配的消息队列
 * - 后台线程处理实际的日志写入
 * - 支持多种溢出策略
 * 
 * **线程安全性**：
 * - 多个线程可以同时调用同一个 async_logger 的日志方法
 * - 内部使用线程安全的队列来传递消息
 * 
 * **性能考虑**：
 * - 前台线程只需要复制消息到队列，开销很小
 * - 实际的格式化和写入操作在后台线程执行
 * - 适合高频率日志记录场景
 * 
 * @note 这是一个 final 类，不能被继承
 * @warning 确保在程序退出前正确销毁 async_logger，否则可能丢失队列中的消息
 */
class SPDLOG_API async_logger final : public std::enable_shared_from_this<async_logger>,
                                      public logger {
    friend class details::thread_pool;

public:
    /**
     * @brief 使用迭代器范围构造异步 logger
     * 
     * @details
     * 这是一个模板构造函数，可以接受任何类型的 sink 迭代器。
     * 允许从一个 sink 容器（如 vector、list）创建 logger。
     * 
     * @tparam It 迭代器类型，必须指向 sink_ptr
     * @param logger_name logger 的名称，用于标识和检索
     * @param begin sink 容器的起始迭代器
     * @param end sink 容器的结束迭代器
     * @param tp 线程池的弱引用，用于处理异步日志消息
     * @param overflow_policy 队列溢出时的处理策略，默认为阻塞模式
     * 
     * @note 使用弱引用（weak_ptr）是为了避免循环引用
     * @warning 确保线程池在 logger 的整个生命周期内都有效
     * 
     * @par 使用示例
     * @code
     * std::vector<spdlog::sink_ptr> sinks;
     * sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
     * sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt"));
     * 
     * auto tp = std::make_shared<spdlog::details::thread_pool>(8192, 1);
     * auto logger = std::make_shared<spdlog::async_logger>(
     *     "multi_sink", sinks.begin(), sinks.end(), tp);
     * @endcode
     */
    template <typename It>
    async_logger(std::string logger_name,
                 It begin,
                 It end,
                 std::weak_ptr<details::thread_pool> tp,
                 async_overflow_policy overflow_policy = async_overflow_policy::block)
        : logger(std::move(logger_name), begin, end),
          thread_pool_(std::move(tp)),
          overflow_policy_(overflow_policy) {}

    /**
     * @brief 使用 sink 初始化列表构造异步 logger
     * 
     * @details
     * 这个构造函数允许使用初始化列表语法创建 logger，
     * 使代码更加简洁和易读。
     * 
     * @param logger_name logger 的名称
     * @param sinks_list sink 的初始化列表
     * @param tp 线程池的弱引用
     * @param overflow_policy 队列溢出策略，默认为阻塞模式
     * 
     * @par 使用示例
     * @code
     * auto tp = spdlog::thread_pool();
     * auto logger = std::make_shared<spdlog::async_logger>(
     *     "multi_sink",
     *     {
     *         std::make_shared<spdlog::sinks::stdout_sink_mt>(),
     *         std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt")
     *     },
     *     tp,
     *     spdlog::async_overflow_policy::overrun_oldest
     * );
     * @endcode
     */
    async_logger(std::string logger_name,
                 sinks_init_list sinks_list,
                 std::weak_ptr<details::thread_pool> tp,
                 async_overflow_policy overflow_policy = async_overflow_policy::block);

    /**
     * @brief 使用单个 sink 构造异步 logger
     * 
     * @details
     * 这是最简单的构造方式，适用于只需要一个输出目标的场景。
     * 
     * @param logger_name logger 的名称
     * @param single_sink 单个 sink 的共享指针
     * @param tp 线程池的弱引用
     * @param overflow_policy 队列溢出策略，默认为阻塞模式
     * 
     * @par 使用示例
     * @code
     * auto tp = spdlog::thread_pool();
     * auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("async.log");
     * auto logger = std::make_shared<spdlog::async_logger>(
     *     "async_file", file_sink, tp);
     * @endcode
     */
    async_logger(std::string logger_name,
                 sink_ptr single_sink,
                 std::weak_ptr<details::thread_pool> tp,
                 async_overflow_policy overflow_policy = async_overflow_policy::block);

    /**
     * @brief 克隆当前 logger，创建一个具有新名称的副本
     * 
     * @details
     * 克隆操作会创建一个新的 async_logger，它具有：
     * - 相同的 sink 列表
     * - 相同的日志级别
     * - 相同的格式化器
     * - 相同的线程池
     * - 相同的溢出策略
     * 
     * @param new_name 新 logger 的名称
     * @return 新创建的 logger 的共享指针
     * 
     * @note 克隆的 logger 是独立的，修改一个不会影响另一个
     * 
     * @par 使用示例
     * @code
     * auto logger1 = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
     *     "logger1", "file1.log");
     * 
     * // 克隆 logger1，创建一个新的 logger
     * auto logger2 = logger1->clone("logger2");
     * 
     * // logger2 具有相同的配置，但是独立的实例
     * logger2->info("这是来自 logger2 的消息");
     * @endcode
     */
    std::shared_ptr<logger> clone(std::string new_name) override;

protected:
    /**
     * @brief 将日志消息发送到 sink（内部方法）
     * 
     * @details
     * 这个方法被 logger 基类调用，用于实际处理日志消息。
     * 在 async_logger 中，这个方法会将消息推送到异步队列，
     * 而不是直接写入 sink。
     * 
     * @param msg 要记录的日志消息
     * 
     * @note 这是一个 protected 方法，只能被 logger 基类和派生类调用
     * @warning 不要直接调用此方法，应该使用 info()、debug() 等公共接口
     */
    void sink_it_(const details::log_msg &msg) override;
    
    /**
     * @brief 刷新所有 sink（内部方法）
     * 
     * @details
     * 在 async_logger 中，这个方法会向队列发送一个刷新命令，
     * 后台线程会在处理完所有待处理的消息后执行刷新操作。
     * 
     * @note 这是一个异步操作，方法返回时刷新可能还未完成
     * @warning 如果需要确保刷新完成，应该在程序退出前等待足够的时间
     */
    void flush_() override;
    
    /**
     * @brief 后台线程执行的实际 sink 操作
     * 
     * @details
     * 这个方法在后台线程中被调用，执行实际的日志写入操作。
     * 它会将消息发送到所有配置的 sink。
     * 
     * @param incoming_log_msg 从队列中取出的日志消息
     * 
     * @note 这个方法只在后台线程中执行，不会阻塞前台线程
     */
    void backend_sink_it_(const details::log_msg &incoming_log_msg);
    
    /**
     * @brief 后台线程执行的实际刷新操作
     * 
     * @details
     * 这个方法在后台线程中被调用，执行实际的刷新操作。
     * 它会调用所有 sink 的 flush 方法。
     * 
     * @note 这个方法只在后台线程中执行
     */
    void backend_flush_();

private:
    /**
     * @brief 线程池的弱引用
     * 
     * @details
     * 使用弱引用（weak_ptr）而不是强引用（shared_ptr）是为了：
     * - 避免循环引用（线程池可能也持有 logger 的引用）
     * - 允许线程池独立管理其生命周期
     * 
     * @note 在使用前需要通过 lock() 方法转换为 shared_ptr
     */
    std::weak_ptr<details::thread_pool> thread_pool_;
    
    /**
     * @brief 队列溢出时的处理策略
     * 
     * @details
     * 决定当消息队列满时如何处理新的日志消息：
     * - block: 阻塞直到有空间
     * - overrun_oldest: 丢弃最旧的消息
     * - discard_new: 丢弃新消息
     */
    async_overflow_policy overflow_policy_;
};
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "async_logger-inl.h"
#endif
