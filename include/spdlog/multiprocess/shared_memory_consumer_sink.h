// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/sinks/sink.h>
#include <spdlog/multiprocess/common.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <chrono>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace spdlog {
namespace multiprocess {

// 消费者配置
struct ConsumerConfig {
    std::chrono::milliseconds poll_interval{10};   // 轮询间隔
    std::chrono::milliseconds poll_duration{1000}; // 轮询持续时间（POLLING状态持续时间）
    bool blocking_mode = false;                    // 阻塞模式
    bool destroy_on_exit = true;                   // 退出时是否销毁共享内存
    bool enable_onep_format = false;               // 是否启用OnePet格式（默认false，使用标准格式）
};

// 消费者Sink - 用于主进程从共享内存读取日志并输出到配置的sink
class SharedMemoryConsumerSink {
public:
    // 构造函数
    // @param shm_handle: 共享内存标识符
    // @param output_sinks: 输出目标sink列表
    // @param config: 配置参数
    // @param offset: 共享内存偏移量（用于使用共享内存的某一块区域）
    SharedMemoryConsumerSink(const SharedMemoryHandle& shm_handle,
                            std::vector<spdlog::sink_ptr> output_sinks,
                            const ConsumerConfig& config = ConsumerConfig(),
                            size_t offset = 0);
    
    // 析构函数
    ~SharedMemoryConsumerSink();
    
    // 启动消费者线程
    void start();
    
    // 停止消费者线程
    void stop();
    
    // 手动轮询一次（用于测试）
    bool poll_once();
    
    // 刷新所有输出sink
    void flush_output_sinks();

private:
    // 消费者线程函数
    void consumer_thread_func();
    
    // 处理一条日志消息
    void process_message(const void* data, size_t size);
    
    std::unique_ptr<LockFreeRingBuffer> ring_buffer_;
    void* shm_ptr_;
    SharedMemoryHandle handle_;
    std::vector<spdlog::sink_ptr> output_sinks_;
    ConsumerConfig config_;
    size_t offset_;           // 共享内存偏移量
    
    std::thread consumer_thread_;
    std::atomic<bool> running_;
};

} // namespace multiprocess
} // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "shared_memory_consumer_sink-inl.h"
#endif

#endif // SPDLOG_ENABLE_MULTIPROCESS
