// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/common.h>
#include <spdlog/details/log_msg.h>

namespace spdlog {

// 缓存行大小常量（用于避免伪共享）
// 大多数现代CPU使用64字节缓存行
constexpr size_t CACHE_LINE_SIZE = 64;

// 无锁环形缓冲区（MPSC模式）
class LockFreeRingBuffer {
public:
    // 初始化环形缓冲区
    // @param memory: 共享内存指针
    // @param total_size: 总大小
    // @param slot_size: 每个槽位的大小
    // @param overflow_policy: 溢出策略（阻塞/丢弃）
    // @param initialize: 是否初始化元数据（消费者为true，生产者为false）
    // @param poll_duration_ms: 轮询持续时间（毫秒），默认1000ms
    LockFreeRingBuffer(void* memory, size_t total_size, size_t slot_size, 
                       OverflowPolicy overflow_policy, bool initialize = true,
                       uint64_t poll_duration_ms = 1000);
    
    // 生产者：预留一个槽位
    // @return: 成功返回槽位索引，失败返回错误码
    Result<size_t> reserve_slot();
    
    // 生产者：尝试预留一个槽位（非阻塞）
    // @return: 成功返回槽位索引，缓冲区满时返回错误
    // 注意：此方法永不阻塞，即使overflow_policy为Block也会立即返回
    Result<size_t> try_reserve_slot();
    
    // 生产者：检查缓冲区是否已满
    // @return: 已满返回true，否则返回false
    bool is_full() const;
    
    // 生产者：写入日志消息到槽位
    // @param slot_index: 槽位索引
    // @param msg: spdlog日志消息对象
    void write_slot(size_t slot_index, const details::log_msg& msg);
    
    // 生产者：写入日志消息到槽位（带进程名和模块名）
    // @param slot_index: 槽位索引
    // @param msg: spdlog日志消息对象
    // @param process_name: 进程名称（最多4字符）
    // @param module_name: 模块名称（最多6字符）
    void write_slot(size_t slot_index, const details::log_msg& msg,
                   const char* process_name, const char* module_name);
    
    // 生产者：提交槽位并通知消费者
    // @param slot_index: 槽位索引
    void commit_slot(size_t slot_index);
    
    // 生产者：通知消费者有新数据（使用eventfd）
    void notify_consumer();
    
    // 消费者：等待通知或超时
    // @param timeout_ms: 超时时间（毫秒），0表示非阻塞
    // @return: 有数据返回true，超时返回false
    bool wait_for_data(int timeout_ms = 0);
    
    // 消费者：检查下一个槽位是否已提交
    // @return: 已提交返回true，否则返回false
    bool is_next_slot_committed();
    
    // 消费者：检测并跳过陈旧的未提交槽位（崩溃恢复）
    // @param stale_threshold_seconds: 陈旧阈值（秒），默认5秒
    // @return: 跳过的陈旧槽位数量
    size_t skip_stale_slots(uint64_t stale_threshold_seconds = 5);
    
    // 消费者：检查下一个槽位是否为陈旧的未提交槽位
    // @param stale_threshold_seconds: 陈旧阈值（秒）
    // @return: 如果是陈旧槽位返回true
    bool is_next_slot_stale(uint64_t stale_threshold_seconds = 5);
    
    // 消费者：读取下一个槽位
    // @param buffer: 输出缓冲区
    // @param max_size: 缓冲区最大大小
    // @return: 成功返回读取的字节数，失败返回错误码
    Result<size_t> read_next_slot(void* buffer, size_t max_size);
    
    // 消费者：释放当前槽位并推进读取索引
    void release_slot();
    
    // 获取缓冲区统计信息
    BufferStats get_stats() const;

    // 槽位结构（公开用于测试）
    // 使用alignas确保整个槽位结构对齐到缓存行边界
    struct alignas(CACHE_LINE_SIZE) Slot {
        std::atomic<bool> committed;              // 提交标志
        uint32_t length;                          // 消息长度
        uint64_t timestamp;                       // 时间戳（纳秒）
        uint8_t level;                            // 日志级别
        uint32_t pid;                             // 进程ID
        uint64_t thread_id;                       // 线程ID
        char process_name[8];                     // 进程名称（4字符 + 填充）
        char module_name[8];                      // 模块名称（6字符 + 填充）
        char logger_name[64];                     // Logger名称
        char payload[];                           // 消息内容（变长）
    };

private:
    // 消费者状态枚举
    enum class ConsumerState : uint32_t {
        WAITING = 0,    // 等待通知状态
        POLLING = 1     // 轮询状态（30秒内不需要通知）
    };
    
    // 元数据（位于共享内存开头）
    // 使用alignas确保整个结构对齐到缓存行边界
    struct alignas(CACHE_LINE_SIZE) Metadata {
        // 只读字段（初始化后不变）- 放在一起减少缓存行占用
        uint32_t version;                    // 版本号
        uint32_t capacity;                   // 槽位数量
        uint32_t slot_size;                  // 槽位大小
        OverflowPolicy overflow_policy;      // 溢出策略
        int eventfd;                         // eventfd文件描述符（Linux）或kqueue fd（macOS）
        
        // 使用alignas(CACHE_LINE_SIZE)确保每个原子变量独占一个缓存行，避免伪共享
        // 写入索引：主要由生产者写入
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_index;
        
        // 读取索引：主要由消费者写入
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_index;
        
        // 消费者状态：由消费者写入，生产者读取
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> consumer_state;
        
        // 上次轮询时间：由消费者写入，生产者读取
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> last_poll_time_ns;
    };
    
    // 获取指定索引的槽位指针
    Slot* get_slot(size_t index) const {
        return reinterpret_cast<Slot*>(static_cast<char*>(slots_base_) + index * slot_size_);
    }
    
    Metadata* metadata_;
    void* slots_base_;
    size_t slot_count_;
    size_t slot_size_;
    int eventfd_;  // 本地eventfd/kqueue副本（用于快速访问）
    
    // 轮询持续时间（纳秒），从 ConsumerConfig.poll_duration 传入
    uint64_t polling_duration_ns_;
};

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
