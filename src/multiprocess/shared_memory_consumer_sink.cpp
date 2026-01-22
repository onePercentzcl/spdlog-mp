// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/log_msg_buffer.h>
#include <thread>

namespace spdlog {
namespace multiprocess {

SharedMemoryConsumerSink::SharedMemoryConsumerSink(
    const SharedMemoryHandle& shm_handle,
    std::vector<spdlog::sink_ptr> output_sinks,
    const ConsumerConfig& config,
    size_t offset)
    : shm_ptr_(nullptr), 
      handle_(shm_handle), 
      output_sinks_(std::move(output_sinks)),
      config_(config),
      offset_(offset),
      running_(false) {
    
    // 映射到共享内存
    auto attach_result = SharedMemoryManager::attach(handle_);
    if (attach_result.is_error()) {
        throw_spdlog_ex("Failed to attach to shared memory: " + attach_result.error_message());
    }
    
    shm_ptr_ = attach_result.value();
    
    // 计算实际使用的内存区域（应用偏移量）
    void* effective_ptr = static_cast<char*>(shm_ptr_) + offset_;
    
    // 计算日志缓存区大小：如果配置了 log_shm_size 则使用，否则使用 handle_.size - offset_
    size_t effective_size = (config_.log_shm_size > 0) ? config_.log_shm_size : (handle_.size - offset_);
    
    // 处理通知模式配置
    NotifyMode effective_notify_mode = config_.notify_mode;
    std::string effective_uds_path = config_.uds_path;
    
#ifdef __APPLE__
    // macOS 上 EventFD 不可用，自动回退到 UDS
    if (effective_notify_mode == NotifyMode::EventFD) {
        fprintf(stderr, "[spdlog::multiprocess] Warning: EventFD not supported on macOS, falling back to UDS\n");
        effective_notify_mode = NotifyMode::UDS;
    }
#endif
    
    // 如果使用 UDS 模式且未指定路径，自动生成默认路径
    if (effective_notify_mode == NotifyMode::UDS && effective_uds_path.empty()) {
        effective_uds_path = generate_default_uds_path(handle_.name);
    }
    
    // 创建环形缓冲区（消费者应该初始化共享内存）
    ring_buffer_ = spdlog::details::make_unique<LockFreeRingBuffer>(
        effective_ptr, 
        effective_size, 
        config_.slot_size,  // 使用配置的槽位大小
        OverflowPolicy::Drop,  // 消费者不关心溢出策略
        true,  // 初始化元数据（消费者负责初始化）
        static_cast<uint64_t>(config_.poll_duration.count()),  // 传递轮询持续时间（毫秒）
        effective_notify_mode,  // 通知模式
        effective_uds_path      // UDS 路径
    );
}

SharedMemoryConsumerSink::~SharedMemoryConsumerSink() {
    // 停止消费者线程（会处理所有剩余消息）
    stop();
    
    // 刷新所有输出sink，确保所有消息都被写入
    flush_output_sinks();
    
    // 从共享内存分离
    if (shm_ptr_) {
        SharedMemoryManager::detach(shm_ptr_, handle_.size);
        shm_ptr_ = nullptr;
    }
    
    // 根据配置决定是否销毁共享内存段
    if (config_.destroy_on_exit) {
        SharedMemoryManager::destroy(handle_);
    }
}

// 启动消费者线程
void SharedMemoryConsumerSink::start() {
    if (running_.load()) {
        return;  // 已经在运行
    }
    
    running_.store(true);
    consumer_thread_ = std::thread(&SharedMemoryConsumerSink::consumer_thread_func, this);
}

// 停止消费者线程
void SharedMemoryConsumerSink::stop() {
    if (!running_.load()) {
        return;  // 已经停止
    }
    
    running_.store(false);
    
    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

// 刷新所有输出sink
void SharedMemoryConsumerSink::flush_output_sinks() {
    for (auto& sink : output_sinks_) {
        try {
            sink->flush();
        } catch (...) {
            // 忽略刷新错误，确保清理继续进行
        }
    }
}

// 消费者线程函数
void SharedMemoryConsumerSink::consumer_thread_func() {
    // 用于定期 flush 的计数器
    int flush_counter = 0;
    const int flush_interval = 100;  // 每 100 次循环 flush 一次（约 1 秒）
    
    while (running_.load()) {
        // 使用wait_for_data等待新数据，结合短时间轮询
        // 超时时间设置为poll_interval，这样可以定期检查running_标志
        int timeout_ms = static_cast<int>(config_.poll_interval.count());
        
        // 等待数据或超时
        bool has_data = ring_buffer_->wait_for_data(timeout_ms);
        
        if (has_data) {
            // 有数据，处理所有可用的消息
            while (ring_buffer_->is_next_slot_committed() && running_.load()) {
                poll_once();
            }
            
            // 检测并跳过陈旧的未提交槽位（崩溃恢复）
            size_t skipped = ring_buffer_->skip_stale_slots();
            if (skipped > 0) {
                // 记录警告日志
                // 注意：这里不能使用spdlog记录，因为可能导致递归
                // 在实际应用中，可以使用stderr或其他方式记录
                // fprintf(stderr, "[spdlog::multiprocess] Skipped %zu stale uncommitted slots (crash recovery)\n", skipped);
            }
        } else {
            // 没有数据时也检查陈旧槽位
            size_t skipped = ring_buffer_->skip_stale_slots();
            (void)skipped;  // 避免未使用警告
        }
        
        // 定期 flush 输出 sink，确保日志及时写入文件
        if (++flush_counter >= flush_interval) {
            flush_counter = 0;
            flush_output_sinks();
        }
    }
    
    // 在退出前处理所有剩余的消息
    while (ring_buffer_->is_next_slot_committed()) {
        poll_once();
    }
    
    // 最后一次检查陈旧槽位
    ring_buffer_->skip_stale_slots();
    
    // 最终 flush
    flush_output_sinks();
}

// 手动轮询一次
bool SharedMemoryConsumerSink::poll_once() {
    // 检查下一个槽位是否已提交
    if (!ring_buffer_->is_next_slot_committed()) {
        return false;
    }
    
    // 读取下一个槽位（使用配置的槽位大小）
    std::vector<char> read_buffer(config_.slot_size);
    auto read_result = ring_buffer_->read_next_slot(read_buffer.data(), read_buffer.size());
    
    if (read_result.is_error()) {
        // 读取失败，记录错误并继续
        return false;
    }
    
    // 处理消息
    process_message(read_buffer.data(), read_result.value());
    
    // 释放槽位
    ring_buffer_->release_slot();
    
    return true;
}

// 处理一条日志消息
void SharedMemoryConsumerSink::process_message(const void* data, size_t size) {
    // 解析槽位数据
    auto* slot = static_cast<const LockFreeRingBuffer::Slot*>(data);
    
    // 重建log_msg对象
    // 从槽位中提取信息
    std::string payload(slot->payload, slot->length);
    
    // 创建时间点（从纳秒转换）
    log_clock::time_point tp = log_clock::time_point(
        std::chrono::duration_cast<log_clock::duration>(
            std::chrono::nanoseconds(slot->timestamp)
        )
    );
    
    // 创建日志级别
    level::level_enum lvl = static_cast<level::level_enum>(slot->level);
    
    std::string logger_name;
    
    if (config_.enable_onep_format) {
        // OnePet格式：包含进程名、模块名、PID、线程ID
        std::string process_name(slot->process_name);
        std::string module_name(slot->module_name);
        
        // ANSI颜色代码
        static const char* WARN_COLOR = "\033[1;33m";  // 亮黄色（与WARN相同）
        static const char* RESET_COLOR = "\033[0m";
        
        // 标记是否为NULL
        bool process_is_null = process_name.empty();
        bool module_is_null = module_name.empty();
        
        // 如果未设置进程名或模块名，使用NULL
        if (process_is_null) {
            process_name = "NULL";
        }
        if (module_is_null) {
            module_name = "NULL";
        }
        
        // 进程名固定4字符，不足补空格
        if (process_name.length() < 4) {
            process_name.append(4 - process_name.length(), ' ');
        } else if (process_name.length() > 4) {
            process_name = process_name.substr(0, 4);
        }
        
        // 模块名固定6字符，居中显示
        if (module_name.length() < 6) {
            size_t totalPadding = 6 - module_name.length();
            size_t leftPadding = totalPadding / 2;
            size_t rightPadding = totalPadding - leftPadding;
            module_name = std::string(leftPadding, ' ') + module_name + std::string(rightPadding, ' ');
        } else if (module_name.length() > 6) {
            module_name = module_name.substr(0, 6);
        }
        
        // 构建logger名称
        std::string pid_str = std::to_string(slot->pid);
        std::string tid_str = std::to_string(slot->thread_id);
        
        // 为控制台输出添加NULL颜色
        std::string console_process_name = process_is_null 
            ? std::string(WARN_COLOR) + process_name + RESET_COLOR 
            : process_name;
        std::string console_module_name = module_is_null 
            ? std::string(WARN_COLOR) + module_name + RESET_COLOR 
            : module_name;
        
        // 文件格式：始终包含PID和线程ID（无颜色）
        std::string file_logger_name = process_name + ":" + pid_str + "] [" + module_name + ":" + tid_str;
        
        // 控制台格式：根据配置决定（带颜色）
        std::string console_logger_name;
        if (config_.debug_format) {
            // Debug格式：显示PID和线程ID
            console_logger_name = console_process_name + ":" + pid_str + "] [" + console_module_name + ":" + tid_str;
        } else {
            // Release格式：不显示PID和线程ID
            console_logger_name = console_process_name + "] [" + console_module_name;
        }
        
        // 输出到所有配置的sink
        for (size_t i = 0; i < output_sinks_.size(); ++i) {
            auto& sink = output_sinks_[i];
            if (!sink->should_log(lvl)) {
                continue;
            }
            
            // 选择logger名称：第一个sink（控制台）使用console格式，其余使用file格式
            const std::string& name = (i == 0 && output_sinks_.size() > 1) 
                ? console_logger_name 
                : file_logger_name;
            
            // 创建log_msg
            details::log_msg msg(
                tp,
                source_loc{},
                string_view_t(name),
                lvl,
                string_view_t(payload)
            );
            
            // 设置线程ID（从槽位中读取的原始线程ID）
            msg.thread_id = slot->thread_id;
            
            sink->log(msg);
        }
    } else {
        // 标准格式：简单的logger名称
        std::string original_logger_name(slot->logger_name);
        if (original_logger_name.empty()) {
            original_logger_name = "default";
        }
        
        // 输出到所有配置的sink
        for (auto& sink : output_sinks_) {
            if (!sink->should_log(lvl)) {
                continue;
            }
            
            // 创建log_msg
            details::log_msg msg(
                tp,
                source_loc{},
                string_view_t(original_logger_name),
                lvl,
                string_view_t(payload)
            );
            
            // 设置线程ID
            msg.thread_id = slot->thread_id;
            
            sink->log(msg);
        }
    }
}

} // namespace multiprocess
} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
