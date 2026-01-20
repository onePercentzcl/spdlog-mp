/**
 * @file async.h
 * @brief 异步日志功能 - 使用全局线程池的异步日志记录
 * @author Gabi Melman 及 spdlog 贡献者
 * @copyright 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
 *            根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)
 * 
 * @details
 * 异步日志是 spdlog 的高级功能，可以显著提高日志记录的性能。
 * 
 * 工作原理：
 * 1. 所有异步 logger 共享一个全局线程池
 * 2. 日志消息被推送到无锁队列中
 * 3. 后台线程从队列中取出消息并写入到目标
 * 4. 主线程不会被 I/O 操作阻塞
 * 
 * 优点：
 * - 极高的性能：主线程几乎不会被日志记录阻塞
 * - 适合高频率日志记录的场景
 * - 不影响主程序的响应速度
 * 
 * 注意事项：
 * - 日志消息不会立即写入，存在延迟
 * - 程序崩溃时可能丢失队列中的消息
 * - 需要额外的内存来存储队列
 * - 线程池需要在程序退出前正确关闭
 * 
 * 生命周期管理：
 * - 每条日志消息都持有 logger 的共享指针
 * - 如果 logger 被删除但队列中还有待处理的消息，logger 的实际销毁会延迟
 * - 这确保了所有消息都能被正确处理
 * 
 * @par 基本使用示例：
 * @code
 * #include "spdlog/async.h"
 * #include "spdlog/sinks/basic_file_sink.h"
 * 
 * int main() {
 *     // 创建异步 logger
 *     auto async_file = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
 *         "async_logger", "logs/async.log"
 *     );
 *     
 *     // 记录日志（不会阻塞）
 *     for (int i = 0; i < 100000; ++i) {
 *         async_file->info("异步日志 #{}", i);
 *     }
 *     
 *     // 确保所有日志都被写入
 *     spdlog::shutdown();
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @par 自定义线程池示例：
 * @code
 * // 初始化线程池：队列大小 32768，2 个工作线程
 * spdlog::init_thread_pool(32768, 2);
 * 
 * // 创建异步 logger
 * auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("async");
 * @endcode
 */

#pragma once

//
// 使用全局线程池的异步日志记录
// 此处创建的所有 logger 共享同一个全局线程池。
// 每条日志消息都与指向 logger 的共享指针一起推送到队列。
// 如果 logger 在队列中有待处理的消息时被删除，其实际销毁将延迟
// 直到线程池处理完所有消息。
// 这是因为队列中的每条消息都持有指向原始 logger 的共享指针。

#include <spdlog/async_logger.h>
#include <spdlog/details/registry.h>
#include <spdlog/details/thread_pool.h>

#include <functional>
#include <memory>
#include <mutex>

/**
 * @namespace spdlog
 * @brief spdlog 库的主命名空间
 */
namespace spdlog {

/**
 * @namespace details
 * @brief spdlog 内部实现细节命名空间
 */
namespace details {
/**
 * @brief 默认的异步队列大小
 * @details 
 * 当创建异步 logger 时，如果没有显式初始化线程池，
 * 会使用此默认值作为队列大小。
 * 
 * 队列大小说明：
 * - 8192 个槽位可以存储 8192 条日志消息
 * - 如果队列满了，行为取决于溢出策略：
 *   - block: 阻塞直到有空间
 *   - overrun_oldest: 覆盖最旧的消息
 * 
 * @note 可以通过 init_thread_pool() 自定义队列大小
 */
static const size_t default_async_q_size = 8192;
}

/**
 * @struct async_factory_impl
 * @brief 异步 logger 工厂模板
 * @tparam OverflowPolicy 队列溢出策略
 * 
 * @details
 * 这是一个工厂类模板，用于创建异步 logger。
 * 它会自动管理全局线程池的创建和初始化。
 * 
 * 溢出策略：
 * - async_overflow_policy::block: 队列满时阻塞，等待空间（默认，更安全）
 * - async_overflow_policy::overrun_oldest: 队列满时覆盖最旧的消息（更快，但可能丢失日志）
 * 
 * @note 通常不需要直接使用此类，而是使用 create_async() 等便捷函数
 */
template <async_overflow_policy OverflowPolicy = async_overflow_policy::block>
struct async_factory_impl {
    /**
     * @brief 创建异步 logger
     * @tparam Sink Sink 类型
     * @tparam SinkArgs Sink 构造函数参数类型
     * @param logger_name Logger 名称
     * @param args Sink 构造函数参数
     * @return 指向新创建的异步 logger 的智能指针
     * 
     * @details
     * 此方法会：
     * 1. 检查全局线程池是否存在，如果不存在则创建
     * 2. 使用提供的参数创建 sink
     * 3. 创建异步 logger 并关联到线程池
     * 4. 初始化并注册 logger
     * 
     * @note 线程池使用默认配置：队列大小 8192，1 个工作线程
     */
    template <typename Sink, typename... SinkArgs>
    static std::shared_ptr<async_logger> create(std::string logger_name, SinkArgs &&...args) {
        auto &registry_inst = details::registry::instance();

        // 如果全局线程池不存在，创建它
        auto &mutex = registry_inst.tp_mutex();
        std::lock_guard<std::recursive_mutex> tp_lock(mutex);
        auto tp = registry_inst.get_tp();
        if (tp == nullptr) {
            tp = std::make_shared<details::thread_pool>(details::default_async_q_size, 1U);
            registry_inst.set_tp(tp);
        }

        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        auto new_logger = std::make_shared<async_logger>(std::move(logger_name), std::move(sink),
                                                         std::move(tp), OverflowPolicy);
        registry_inst.initialize_logger(new_logger);
        return new_logger;
    }
};

/**
 * @typedef async_factory
 * @brief 阻塞策略的异步 logger 工厂
 * @details 
 * 当队列满时，会阻塞等待空间。
 * 这是默认的、更安全的策略，不会丢失日志消息。
 */
using async_factory = async_factory_impl<async_overflow_policy::block>;

/**
 * @typedef async_factory_nonblock
 * @brief 非阻塞策略的异步 logger 工厂
 * @details 
 * 当队列满时，会覆盖最旧的消息。
 * 这种策略性能更好，但在高负载下可能丢失日志消息。
 */
using async_factory_nonblock = async_factory_impl<async_overflow_policy::overrun_oldest>;

/**
 * @brief 创建异步 logger（阻塞策略）
 * @tparam Sink Sink 类型
 * @tparam SinkArgs Sink 构造函数参数类型
 * @param logger_name Logger 名称
 * @param sink_args Sink 构造函数参数
 * @return 指向新创建的 logger 的智能指针
 * 
 * @details
 * 这是创建异步 logger 的便捷函数。
 * 使用阻塞策略：当队列满时，会阻塞等待空间。
 * 
 * 特点：
 * - 不会丢失日志消息
 * - 在高负载下可能阻塞主线程
 * - 适合对日志完整性要求高的场景
 * 
 * @note
 * - 如果全局线程池不存在，会自动创建（队列大小 8192，1 个线程）
 * - 可以通过 init_thread_pool() 预先初始化自定义的线程池
 * 
 * @par 使用示例：
 * @code
 * // 创建异步文件 logger
 * auto async_file = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
 *     "async_file", "logs/async.log"
 * );
 * 
 * // 创建异步控制台 logger
 * auto async_console = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>(
 *     "async_console"
 * );
 * 
 * // 记录日志（不会阻塞，除非队列满）
 * async_file->info("异步日志消息");
 * @endcode
 * 
 * @see create_async_nb() 非阻塞版本
 * @see init_thread_pool() 初始化自定义线程池
 */
template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<spdlog::logger> create_async(std::string logger_name,
                                                    SinkArgs &&...sink_args) {
    return async_factory::create<Sink>(std::move(logger_name),
                                       std::forward<SinkArgs>(sink_args)...);
}

/**
 * @brief 创建异步 logger（非阻塞策略）
 * @tparam Sink Sink 类型
 * @tparam SinkArgs Sink 构造函数参数类型
 * @param logger_name Logger 名称
 * @param sink_args Sink 构造函数参数
 * @return 指向新创建的 logger 的智能指针
 * 
 * @details
 * 这是创建异步 logger 的便捷函数。
 * 使用非阻塞策略：当队列满时，会覆盖最旧的消息。
 * 
 * 特点：
 * - 永不阻塞主线程
 * - 在高负载下可能丢失旧的日志消息
 * - 适合对性能要求极高的场景
 * 
 * @warning 在高负载下可能丢失日志消息，请谨慎使用
 * 
 * @note
 * - 如果全局线程池不存在，会自动创建（队列大小 8192，1 个线程）
 * - 可以通过 init_thread_pool() 预先初始化自定义的线程池
 * 
 * @par 使用示例：
 * @code
 * // 创建非阻塞异步 logger
 * auto async_nb = spdlog::create_async_nb<spdlog::sinks::basic_file_sink_mt>(
 *     "async_nb", "logs/async_nb.log"
 * );
 * 
 * // 记录日志（永不阻塞，但可能丢失消息）
 * for (int i = 0; i < 1000000; ++i) {
 *     async_nb->info("高频日志 #{}", i);
 * }
 * @endcode
 * 
 * @see create_async() 阻塞版本（更安全）
 * @see init_thread_pool() 初始化自定义线程池
 */
template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<spdlog::logger> create_async_nb(std::string logger_name,
                                                       SinkArgs &&...sink_args) {
    return async_factory_nonblock::create<Sink>(std::move(logger_name),
                                                std::forward<SinkArgs>(sink_args)...);
}

/**
 * @brief 初始化全局线程池（完整版本）
 * @param q_size 队列大小（可以存储的日志消息数量）
 * @param thread_count 工作线程数量
 * @param on_thread_start 线程启动时的回调函数
 * @param on_thread_stop 线程停止时的回调函数
 * 
 * @details
 * 初始化全局线程池，所有异步 logger 都会使用这个线程池。
 * 
 * 参数说明：
 * - q_size: 队列大小，建议设置为 2 的幂次方以获得最佳性能
 * - thread_count: 工作线程数量，通常 1-2 个线程就足够了
 * - on_thread_start: 每个工作线程启动时调用，可用于设置线程名称、优先级等
 * - on_thread_stop: 每个工作线程停止时调用，可用于清理资源
 * 
 * @note
 * - 必须在创建任何异步 logger 之前调用
 * - 如果已经存在线程池，会被新的线程池替换
 * - 线程池会在程序退出时自动清理
 * 
 * @par 使用示例：
 * @code
 * // 初始化线程池：队列大小 32768，2 个工作线程
 * spdlog::init_thread_pool(32768, 2,
 *     []() {
 *         // 线程启动时设置线程名称
 *         pthread_setname_np(pthread_self(), "spdlog_worker");
 *     },
 *     []() {
 *         // 线程停止时的清理工作
 *         std::cout << "工作线程停止" << std::endl;
 *     }
 * );
 * 
 * // 创建异步 logger
 * auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("async");
 * @endcode
 * 
 * @see create_async() 创建异步 logger
 * @see thread_pool() 获取全局线程池
 */
inline void init_thread_pool(size_t q_size,
                             size_t thread_count,
                             std::function<void()> on_thread_start,
                             std::function<void()> on_thread_stop) {
    auto tp = std::make_shared<details::thread_pool>(q_size, thread_count, on_thread_start,
                                                     on_thread_stop);
    details::registry::instance().set_tp(std::move(tp));
}

/**
 * @brief 初始化全局线程池（带启动回调）
 * @param q_size 队列大小
 * @param thread_count 工作线程数量
 * @param on_thread_start 线程启动时的回调函数
 * 
 * @details
 * 初始化全局线程池，不设置线程停止回调。
 * 
 * @par 使用示例：
 * @code
 * // 初始化线程池，只设置启动回调
 * spdlog::init_thread_pool(16384, 1, []() {
 *     std::cout << "异步日志线程启动" << std::endl;
 * });
 * @endcode
 * 
 * @see init_thread_pool(size_t, size_t, std::function<void()>, std::function<void()>) 完整版本
 */
inline void init_thread_pool(size_t q_size,
                             size_t thread_count,
                             std::function<void()> on_thread_start) {
    init_thread_pool(q_size, thread_count, on_thread_start, [] {});
}

/**
 * @brief 初始化全局线程池（基础版本）
 * @param q_size 队列大小
 * @param thread_count 工作线程数量
 * 
 * @details
 * 初始化全局线程池，不设置任何回调函数。
 * 这是最常用的版本。
 * 
 * 队列大小建议：
 * - 低频日志（< 1000 条/秒）：8192（默认）
 * - 中频日志（1000-10000 条/秒）：16384 或 32768
 * - 高频日志（> 10000 条/秒）：65536 或更大
 * 
 * 线程数量建议：
 * - 单个文件输出：1 个线程
 * - 多个输出目标：2 个线程
 * - 通常不需要超过 2 个线程
 * 
 * @par 使用示例：
 * @code
 * // 初始化线程池：队列大小 16384，1 个工作线程
 * spdlog::init_thread_pool(16384, 1);
 * 
 * // 创建异步 logger
 * auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
 *     "async", "logs/async.log"
 * );
 * @endcode
 * 
 * @see init_thread_pool(size_t, size_t, std::function<void()>) 带启动回调的版本
 */
inline void init_thread_pool(size_t q_size, size_t thread_count) {
    init_thread_pool(q_size, thread_count, [] {}, [] {});
}

/**
 * @brief 获取全局线程池
 * @return 指向全局线程池的智能指针，如果不存在则返回 nullptr
 * 
 * @details
 * 获取当前的全局线程池对象。
 * 可以用于检查线程池是否已初始化，或访问线程池的高级功能。
 * 
 * @note
 * - 如果还没有创建任何异步 logger 且没有调用 init_thread_pool()，返回 nullptr
 * - 通常不需要直接访问线程池对象
 * 
 * @par 使用示例：
 * @code
 * // 检查线程池是否已初始化
 * auto tp = spdlog::thread_pool();
 * if (tp) {
 *     std::cout << "线程池已初始化" << std::endl;
 * } else {
 *     std::cout << "线程池未初始化" << std::endl;
 * }
 * @endcode
 * 
 * @see init_thread_pool() 初始化线程池
 */
inline std::shared_ptr<spdlog::details::thread_pool> thread_pool() {
    return details::registry::instance().get_tp();
}

}  // namespace spdlog
