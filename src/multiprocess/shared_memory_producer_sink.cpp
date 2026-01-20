// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/mode.h>
#include <spdlog/multiprocess/custom_formatter.h>

namespace spdlog {
namespace multiprocess {

template<typename Mutex>
SharedMemoryProducerSink<Mutex>::SharedMemoryProducerSink(
    const SharedMemoryHandle& shm_handle,
    const ProducerConfig& config,
    size_t offset)
    : shm_ptr_(nullptr), handle_(shm_handle), config_(config), 
      offset_(offset), shm_available_(false), using_fallback_(false) {
    
    // 尝试映射到共享内存
    auto attach_result = SharedMemoryManager::attach(handle_);
    if (attach_result.is_error()) {
        // 共享内存不可用
        if (config_.enable_fallback && config_.fallback_sink) {
            // 启用回退模式
            using_fallback_ = true;
            shm_available_ = false;
            return;
        }
        // 没有回退选项，抛出异常
        throw_spdlog_ex("Failed to attach to shared memory: " + attach_result.error_message());
    }
    
    shm_ptr_ = attach_result.value();
    shm_available_ = true;
    
    // 计算实际使用的内存区域（应用偏移量）
    void* effective_ptr = static_cast<char*>(shm_ptr_) + offset_;
    size_t effective_size = handle_.size - offset_;
    
    // 创建环形缓冲区（不初始化，因为消费者已经初始化过了）
    ring_buffer_ = spdlog::details::make_unique<LockFreeRingBuffer>(
        effective_ptr, 
        effective_size, 
        config_.slot_size, 
        config_.overflow_policy,
        false  // 不初始化元数据
    );
}

template<typename Mutex>
SharedMemoryProducerSink<Mutex>::~SharedMemoryProducerSink() {
    // 从共享内存分离
    if (shm_ptr_) {
        SharedMemoryManager::detach(shm_ptr_, handle_.size);
        shm_ptr_ = nullptr;
    }
}

template<typename Mutex>
void SharedMemoryProducerSink<Mutex>::sink_it_(const details::log_msg& msg) {
    // 检查多进程模式是否启用
    if (!multiprocess::is_enabled()) {
        // 多进程模式已禁用，使用回退sink或丢弃消息
        if (config_.enable_fallback && config_.fallback_sink) {
            config_.fallback_sink->log(msg);
        }
        return;
    }
    
    // 如果正在使用回退模式，直接使用回退sink
    if (using_fallback_ && config_.fallback_sink) {
        config_.fallback_sink->log(msg);
        return;
    }
    
    // 如果共享内存不可用，检查是否有回退选项
    if (!shm_available_) {
        if (config_.enable_fallback && config_.fallback_sink) {
            config_.fallback_sink->log(msg);
        }
        return;
    }
    
    // 预留槽位
    auto reserve_result = ring_buffer_->reserve_slot();
    if (reserve_result.is_error()) {
        // 缓冲区满或其他错误
        if (config_.enable_fallback && config_.fallback_sink) {
            // 使用回退sink
            config_.fallback_sink->log(msg);
        }
        // 否则消息被丢弃
        return;
    }
    
    size_t slot_idx = reserve_result.value();
    
    // 获取全局进程名和模块名
    std::string process_name = GetProcessName();
    std::string module_name = GetModuleName(msg.thread_id);
    
    // 写入数据（带进程名和模块名）
    ring_buffer_->write_slot(slot_idx, msg, process_name.c_str(), module_name.c_str());
    
    // 提交槽位
    ring_buffer_->commit_slot(slot_idx);
}

template<typename Mutex>
void SharedMemoryProducerSink<Mutex>::flush_() {
    // 如果正在使用回退模式，刷新回退sink
    if (using_fallback_ && config_.fallback_sink) {
        config_.fallback_sink->flush();
        return;
    }
    
    // 生产者不需要刷新操作
    // 所有消息在commit_slot时已经对消费者可见
}

// 显式实例化模板
template class SharedMemoryProducerSink<std::mutex>;
template class SharedMemoryProducerSink<details::null_mutex>;

} // namespace multiprocess
} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
