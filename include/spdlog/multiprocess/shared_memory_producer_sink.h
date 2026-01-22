// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/sinks/base_sink.h>
#include <spdlog/multiprocess/common.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/multiprocess/mode.h>
#include <chrono>
#include <memory>
#include <mutex>

namespace spdlog {
namespace multiprocess {

// 生产者配置
struct ProducerConfig {
    size_t slot_size = 4096;                                    // 槽位大小
    OverflowPolicy overflow_policy = OverflowPolicy::Block;     // 溢出策略
    std::chrono::milliseconds block_timeout{1000};              // 阻塞超时
    bool enable_fallback = false;                               // 是否启用回退机制
    spdlog::sink_ptr fallback_sink = nullptr;                   // 回退sink（共享内存不可用时使用）
    
    // 通知模式配置
    NotifyMode notify_mode = NotifyMode::UDS;                   // 通知模式（默认 UDS）
    std::string uds_path;                                       // UDS 路径（空则从共享内存元数据读取）
    int eventfd = -1;                                           // eventfd 文件描述符（可选，仅 EventFD 模式）
};

// 生产者Sink - 用于子进程写入日志到共享内存
template<typename Mutex>
class SharedMemoryProducerSink : public spdlog::sinks::base_sink<Mutex> {
public:
    // 构造函数
    // @param shm_handle: 共享内存标识符
    // @param config: 配置参数
    // @param offset: 共享内存偏移量（用于使用共享内存的某一块区域）
    explicit SharedMemoryProducerSink(const SharedMemoryHandle& shm_handle,
                                      const ProducerConfig& config = ProducerConfig(),
                                      size_t offset = 0);
    
    // 析构函数
    ~SharedMemoryProducerSink() override;
    
    // 检查共享内存是否可用
    bool is_shared_memory_available() const { return shm_available_; }
    
    // 检查是否正在使用回退模式
    bool is_using_fallback() const { return using_fallback_; }

protected:
    // 实现base_sink的虚函数
    void sink_it_(const details::log_msg& msg) override;
    void flush_() override;

private:
    std::unique_ptr<LockFreeRingBuffer> ring_buffer_;
    void* shm_ptr_;
    SharedMemoryHandle handle_;
    ProducerConfig config_;
    size_t offset_;           // 共享内存偏移量
    bool shm_available_;      // 共享内存是否可用
    bool using_fallback_;     // 是否正在使用回退模式
};

// 多线程版本
using shared_memory_producer_sink_mt = SharedMemoryProducerSink<std::mutex>;

// 单线程版本
using shared_memory_producer_sink_st = SharedMemoryProducerSink<details::null_mutex>;

} // namespace multiprocess
} // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "shared_memory_producer_sink-inl.h"
#endif

#endif // SPDLOG_ENABLE_MULTIPROCESS
