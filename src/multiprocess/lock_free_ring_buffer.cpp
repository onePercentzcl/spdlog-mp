// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/details/os.h>
#include <cstring>
#include <thread>
#include <chrono>

// Platform-specific includes for notification
#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#else
// 非 Linux 平台（macOS/FreeBSD 等）：仅使用 UDS 模式
// 不需要 eventfd 或 kqueue 相关头文件
#include <unistd.h>
#endif

// UDS (Unix Domain Socket) includes
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

namespace spdlog {

// 消费者状态枚举定义
enum class ConsumerState : uint32_t {
    WAITING = 0,    // 等待通知状态
    POLLING = 1     // 轮询状态（30秒内不需要通知）
};

LockFreeRingBuffer::LockFreeRingBuffer(void* memory, size_t total_size, size_t slot_size, 
                                       OverflowPolicy overflow_policy, bool initialize,
                                       uint64_t poll_duration_ms,
                                       NotifyMode notify_mode,
                                       const std::string& uds_path)
    : metadata_(nullptr), slots_base_(nullptr), slot_count_(0), slot_size_(slot_size), notify_fd_(-1),
      uds_server_fd_(-1), uds_client_fd_(-1), is_consumer_(initialize), uds_path_(),
      notify_mode_(notify_mode),  // 保存通知模式副本
      polling_duration_ns_(poll_duration_ms * 1000 * 1000) {  // 转换为纳秒
    // 将共享内存指针转换为元数据指针
    metadata_ = static_cast<Metadata*>(memory);
    
    // 计算槽位数组的起始位置（元数据之后）
    size_t metadata_size = sizeof(Metadata);
    // 对齐到缓存行边界
    metadata_size = (metadata_size + 63) & ~63;
    slots_base_ = static_cast<char*>(memory) + metadata_size;
    
    // 计算可用的槽位数量
    size_t available_size = total_size - metadata_size;
    slot_count_ = available_size / slot_size_;
    
    if (initialize) {
        // 初始化元数据
        metadata_->version = MULTIPROCESS_VERSION;
        metadata_->capacity = static_cast<uint32_t>(slot_count_);
        metadata_->slot_size = static_cast<uint32_t>(slot_size_);
        metadata_->overflow_policy = overflow_policy;
        
        // 初始化通知模式
        metadata_->notify_mode = notify_mode;
        
        // 初始化 UDS 路径
        std::memset(metadata_->uds_path, 0, sizeof(metadata_->uds_path));
        if (!uds_path.empty() && uds_path.size() < sizeof(metadata_->uds_path)) {
            std::memcpy(metadata_->uds_path, uds_path.c_str(), uds_path.size());
        }
        
        // 根据通知模式创建通知机制
        if (notify_mode == NotifyMode::UDS) {
            // UDS 模式：初始化 UDS 服务端
            if (init_uds_server(uds_path)) {
                notify_fd_ = uds_server_fd_;
                metadata_->notify_fd = uds_server_fd_;
            } else {
                notify_fd_ = -1;
                metadata_->notify_fd = -1;
            }
        } else {
            // EventFD 模式
#ifdef __linux__
            // Linux: 使用eventfd
            notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
            if (notify_fd_ >= 0) {
                metadata_->notify_fd = notify_fd_;
            } else {
                metadata_->notify_fd = -1;
            }
#else
            // 非 Linux 平台（macOS/FreeBSD 等）：EventFD 不可用
            // 注意：正常情况下，消费者 sink 应该已经在配置处理时将 EventFD 回退到 UDS
            // 如果代码执行到这里，说明调用者绕过了 sink 层直接使用 ring buffer
            // 此时设置 notify_fd 为 -1，通知机制将不可用（使用轮询模式）
            // 不打印警告，因为这可能是测试代码或有意为之
            metadata_->notify_fd = -1;
            notify_fd_ = -1;
#endif
        }
        
        // 初始化原子索引为0
        metadata_->write_index.store(0, std::memory_order_relaxed);
        metadata_->read_index.store(0, std::memory_order_relaxed);
        
        // 初始化消费者状态为WAITING
        metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::WAITING), 
                                       std::memory_order_relaxed);
        metadata_->last_poll_time_ns.store(0, std::memory_order_relaxed);
        
        // 初始化所有槽位的提交标志为false
        for (size_t i = 0; i < slot_count_; ++i) {
            Slot* slot = get_slot(i);
            slot->committed.store(false, std::memory_order_relaxed);
            slot->length = 0;
            slot->timestamp = 0;
            slot->level = 0;
            std::memset(slot->logger_name, 0, sizeof(slot->logger_name));
        }
    } else {
        // 生产者：从元数据读取配置
        notify_mode_ = metadata_->notify_mode;  // 保存通知模式副本
        
        if (metadata_->notify_mode == NotifyMode::UDS) {
            // UDS 模式：连接到消费者的 UDS 服务端
            std::string path(metadata_->uds_path);
            if (connect_uds_server(path)) {
                notify_fd_ = uds_client_fd_;
            } else {
                notify_fd_ = -1;
            }
        } else {
            // EventFD/kqueue 模式：从元数据读取 notify_fd
            notify_fd_ = metadata_->notify_fd;
        }
    }
}

LockFreeRingBuffer::~LockFreeRingBuffer() {
    // 根据角色（消费者/生产者）和通知模式清理资源
    // 使用本地保存的 notify_mode_，因为 metadata_ 可能已经无效
    
    if (is_consumer_) {
        // 消费者端清理
        if (notify_mode_ == NotifyMode::UDS) {
            // UDS 模式：关闭服务端 socket 并删除 socket 文件
            if (uds_server_fd_ >= 0) {
                close(uds_server_fd_);
                uds_server_fd_ = -1;
            }
            // 删除 socket 文件
            if (!uds_path_.empty()) {
                unlink(uds_path_.c_str());
            }
        } else {
            // EventFD 模式：关闭 eventfd
#ifdef __linux__
            if (notify_fd_ >= 0) {
                close(notify_fd_);
                notify_fd_ = -1;
            }
#endif
        }
    } else {
        // 生产者端清理
        if (notify_mode_ == NotifyMode::UDS) {
            // UDS 模式：关闭客户端 socket
            if (uds_client_fd_ >= 0) {
                close(uds_client_fd_);
                uds_client_fd_ = -1;
            }
        }
        // EventFD 模式：生产者不拥有 eventfd，不需要关闭
        // （eventfd 由消费者创建和管理）
    }
}

Result<size_t> LockFreeRingBuffer::reserve_slot() {
    // 优化：先检查缓冲区是否已满，避免不必要的原子操作
    // 使用relaxed读取进行快速检查（可能有轻微的过时数据，但不影响正确性）
    uint64_t current_write = metadata_->write_index.load(std::memory_order_relaxed);
    uint64_t current_read = metadata_->read_index.load(std::memory_order_relaxed);
    
    // 快速路径：如果缓冲区明显未满，直接预留槽位
    if (current_write < current_read + metadata_->capacity) {
        // 使用memory_order_relaxed进行fetch_add，因为：
        // 1. 我们只需要原子性，不需要与其他操作同步
        // 2. 后续的write_slot和commit_slot会提供必要的内存屏障
        uint64_t write_idx = metadata_->write_index.fetch_add(1, std::memory_order_relaxed);
        
        // 再次检查是否真的有空间（可能有其他生产者同时预留）
        uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
        
        if (write_idx < read_idx + metadata_->capacity) {
            // 非阻塞成功：立即返回槽位索引
            return Result<size_t>::ok(write_idx % metadata_->capacity);
        }
        
        // 缓冲区已满，根据策略处理
        if (metadata_->overflow_policy == OverflowPolicy::Drop) {
            return Result<size_t>::error("Buffer is full, message dropped");
        }
        
        // 阻塞模式：等待空间可用
        // 使用指数退避策略减少CPU占用
        int spin_count = 0;
        const int MAX_SPIN = 100;
        
        while (write_idx >= metadata_->read_index.load(std::memory_order_acquire) + metadata_->capacity) {
            if (spin_count < MAX_SPIN) {
                // 短暂自旋，适用于低竞争场景
                #if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();  // x86 PAUSE指令
                #elif defined(__aarch64__) || defined(_M_ARM64)
                __asm__ volatile("yield");  // ARM YIELD指令
                #endif
                spin_count++;
            } else {
                // 超过自旋阈值，让出CPU
                std::this_thread::yield();
            }
        }
        
        return Result<size_t>::ok(write_idx % metadata_->capacity);
    }
    
    // 慢速路径：缓冲区可能已满
    if (metadata_->overflow_policy == OverflowPolicy::Drop) {
        // 丢弃模式：再次精确检查
        uint64_t write_idx = metadata_->write_index.load(std::memory_order_acquire);
        uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
        
        if (write_idx >= read_idx + metadata_->capacity) {
            return Result<size_t>::error("Buffer is full, message dropped");
        }
        
        // 有空间了，尝试预留
        write_idx = metadata_->write_index.fetch_add(1, std::memory_order_relaxed);
        return Result<size_t>::ok(write_idx % metadata_->capacity);
    }
    
    // 阻塞模式：等待空间可用后预留
    int spin_count = 0;
    const int MAX_SPIN = 100;
    
    while (true) {
        uint64_t write_idx = metadata_->write_index.load(std::memory_order_acquire);
        uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
        
        if (write_idx < read_idx + metadata_->capacity) {
            // 有空间了，尝试预留
            write_idx = metadata_->write_index.fetch_add(1, std::memory_order_relaxed);
            
            // 再次检查
            read_idx = metadata_->read_index.load(std::memory_order_acquire);
            if (write_idx < read_idx + metadata_->capacity) {
                return Result<size_t>::ok(write_idx % metadata_->capacity);
            }
            
            // 被其他生产者抢占，继续等待
        }
        
        if (spin_count < MAX_SPIN) {
            #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
            #elif defined(__aarch64__) || defined(_M_ARM64)
            __asm__ volatile("yield");
            #endif
            spin_count++;
        } else {
            std::this_thread::yield();
        }
    }
}

Result<size_t> LockFreeRingBuffer::try_reserve_slot() {
    // 非阻塞版本：永不阻塞，缓冲区满时立即返回错误
    // 使用relaxed读取进行快速检查
    uint64_t current_write = metadata_->write_index.load(std::memory_order_relaxed);
    uint64_t current_read = metadata_->read_index.load(std::memory_order_relaxed);
    
    // 快速路径：如果缓冲区明显已满，立即返回
    if (current_write >= current_read + metadata_->capacity) {
        return Result<size_t>::error("Buffer is full");
    }
    
    // 尝试预留槽位
    uint64_t write_idx = metadata_->write_index.fetch_add(1, std::memory_order_relaxed);
    
    // 再次检查是否真的有空间
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
    
    if (write_idx < read_idx + metadata_->capacity) {
        // 成功预留
        return Result<size_t>::ok(write_idx % metadata_->capacity);
    }
    
    // 缓冲区已满（被其他生产者抢占）
    // 注意：这里我们已经递增了write_index，但没有实际使用槽位
    // 这是一个已知的权衡：在高竞争场景下可能会浪费一些槽位
    // 但这保证了非阻塞语义
    return Result<size_t>::error("Buffer is full");
}

bool LockFreeRingBuffer::is_full() const {
    // 使用relaxed读取进行快速检查
    uint64_t write_idx = metadata_->write_index.load(std::memory_order_relaxed);
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_relaxed);
    
    return write_idx >= read_idx + metadata_->capacity;
}

void LockFreeRingBuffer::write_slot(size_t slot_index, const details::log_msg& msg) {
    // 调用带进程名和模块名的版本，使用空字符串
    write_slot(slot_index, msg, "", "");
}

void LockFreeRingBuffer::write_slot(size_t slot_index, const details::log_msg& msg,
                                   const char* process_name, const char* module_name) {
    Slot* slot = get_slot(slot_index);
    
    // 从log_msg中提取时间戳（生产者调用spdlog::info时的时间）
    auto duration = msg.time.time_since_epoch();
    slot->timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    
    // 写入日志级别
    slot->level = static_cast<uint8_t>(msg.level);
    
    // 写入PID（当前进程ID）
    slot->pid = static_cast<uint32_t>(details::os::pid());
    
    // 写入线程ID
    slot->thread_id = msg.thread_id;
    
    // 写入进程名称（最多4字符）
    std::memset(slot->process_name, 0, sizeof(slot->process_name));
    if (process_name && process_name[0] != '\0') {
        size_t len = std::min(strlen(process_name), sizeof(slot->process_name) - 1);
        std::memcpy(slot->process_name, process_name, len);
    }
    
    // 写入模块名称（最多6字符）
    std::memset(slot->module_name, 0, sizeof(slot->module_name));
    if (module_name && module_name[0] != '\0') {
        size_t len = std::min(strlen(module_name), sizeof(slot->module_name) - 1);
        std::memcpy(slot->module_name, module_name, len);
    }
    
    // 写入logger名称
    size_t name_len = std::min(msg.logger_name.size(), sizeof(slot->logger_name) - 1);
    std::memcpy(slot->logger_name, msg.logger_name.data(), name_len);
    slot->logger_name[name_len] = '\0';
    
    // 写入消息内容到payload
    size_t max_payload_size = slot_size_ - sizeof(Slot);
    size_t actual_size = std::min(msg.payload.size(), max_payload_size);
    std::memcpy(slot->payload, msg.payload.data(), actual_size);
    
    // 写入消息长度
    slot->length = static_cast<uint32_t>(actual_size);
}

void LockFreeRingBuffer::commit_slot(size_t slot_index) {
    Slot* slot = get_slot(slot_index);
    
    // 使用memory_order_release确保之前的写入操作对其他线程可见
    // 这保证了在提交标志被设置为true之前，所有的数据写入都已完成
    // 注意：这是唯一需要release语义的地方，因为它建立了与消费者的同步点
    slot->committed.store(true, std::memory_order_release);
    
    // 通知消费者（使用短时间轮询策略）
    notify_consumer();
}

bool LockFreeRingBuffer::is_next_slot_committed() {
    // 优化：使用relaxed读取write_index进行快速检查
    // 因为我们只需要知道是否有数据，不需要精确的同步
    uint64_t write_idx = metadata_->write_index.load(std::memory_order_relaxed);
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_relaxed);
    
    // 快速路径：缓冲区为空
    if (read_idx >= write_idx) {
        return false;
    }
    
    // 获取下一个要读取的槽位
    size_t slot_idx = read_idx % metadata_->capacity;
    Slot* slot = get_slot(slot_idx);
    
    // 使用memory_order_acquire确保能看到生产者的写入
    // 这与commit_slot中的release形成同步对
    bool committed = slot->committed.load(std::memory_order_acquire);
    
    return committed;
}

bool LockFreeRingBuffer::is_next_slot_stale(uint64_t stale_threshold_seconds) {
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
    uint64_t write_idx = metadata_->write_index.load(std::memory_order_acquire);
    
    // 检查缓冲区是否为空
    if (read_idx >= write_idx) {
        return false;
    }
    
    // 获取下一个要读取的槽位
    size_t slot_idx = read_idx % metadata_->capacity;
    Slot* slot = get_slot(slot_idx);
    
    // 检查是否已提交
    bool committed = slot->committed.load(std::memory_order_acquire);
    if (committed) {
        return false;  // 已提交的槽位不是陈旧的
    }
    
    // 检测陈旧的未提交槽位（崩溃恢复）
    // 如果槽位的时间戳很旧（超过阈值），认为是生产者崩溃导致的部分写入
    if (slot->timestamp > 0) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        
        // 计算陈旧阈值（纳秒）
        uint64_t stale_threshold_ns = stale_threshold_seconds * 1000000000ULL;
        
        if (now_ns > slot->timestamp && (now_ns - slot->timestamp) > stale_threshold_ns) {
            return true;  // 陈旧槽位
        }
    }
    
    return false;
}

size_t LockFreeRingBuffer::skip_stale_slots(uint64_t stale_threshold_seconds) {
    size_t skipped_count = 0;
    
    while (is_next_slot_stale(stale_threshold_seconds)) {
        uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
        size_t slot_idx = read_idx % metadata_->capacity;
        Slot* slot = get_slot(slot_idx);
        
        // 重置槽位状态
        slot->committed.store(false, std::memory_order_relaxed);
        slot->length = 0;
        slot->timestamp = 0;
        
        // 推进读取索引
        metadata_->read_index.fetch_add(1, std::memory_order_release);
        
        skipped_count++;
    }
    
    return skipped_count;
}

Result<size_t> LockFreeRingBuffer::read_next_slot(void* buffer, size_t max_size) {
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
    
    // 获取当前要读取的槽位
    size_t slot_idx = read_idx % metadata_->capacity;
    Slot* slot = get_slot(slot_idx);
    
    // 验证槽位已提交
    if (!slot->committed.load(std::memory_order_acquire)) {
        return Result<size_t>::error("Slot not committed");
    }
    
    // 验证消息完整性
    if (slot->length == 0) {
        return Result<size_t>::error("Invalid message: length is zero");
    }
    
    // 检查缓冲区大小是否足够
    size_t total_size = sizeof(Slot) + slot->length;
    if (max_size < total_size) {
        return Result<size_t>::error("Buffer too small");
    }
    
    // 复制完整的槽位数据到输出缓冲区
    // 包括：committed标志、length、timestamp、level、logger_name和payload
    std::memcpy(buffer, slot, total_size);
    
    return Result<size_t>::ok(total_size);
}

void LockFreeRingBuffer::release_slot() {
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
    
    // 获取当前槽位
    size_t slot_idx = read_idx % metadata_->capacity;
    Slot* slot = get_slot(slot_idx);
    
    // 重置槽位的提交标志
    slot->committed.store(false, std::memory_order_relaxed);
    
    // 清空槽位数据（可选，用于调试）
    slot->length = 0;
    slot->timestamp = 0;
    
    // 原子性地递增read_index
    // 使用memory_order_release确保槽位重置对其他线程可见
    metadata_->read_index.fetch_add(1, std::memory_order_release);
}

void LockFreeRingBuffer::notify_consumer() {
    if (notify_fd_ < 0) {
        return;  // 通知机制不可用
    }
    
    // 检查消费者状态
    uint32_t state = metadata_->consumer_state.load(std::memory_order_acquire);
    
    if (state == static_cast<uint32_t>(ConsumerState::POLLING)) {
        // 消费者正在轮询中，检查是否已经超过30秒
        uint64_t last_poll_time = metadata_->last_poll_time_ns.load(std::memory_order_acquire);
        auto now = std::chrono::steady_clock::now();
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        if (now_ns - last_poll_time < polling_duration_ns_) {
            // 还在轮询期内，不需要通知
            return;
        }
        // 超过轮询期了，消费者可能已经进入等待状态，继续发送通知
    }
    
    // 消费者在WAITING状态，或者轮询期已过，发送通知
    // 根据通知模式选择通知方式
    if (metadata_->notify_mode == NotifyMode::UDS) {
        // UDS 模式：通过 UDS socket 发送通知
        notify_via_uds();
    } else {
        // EventFD 模式（仅 Linux 支持）
#ifdef __linux__
        // Linux: 写入eventfd
        uint64_t value = 1;
        ssize_t ret = write(notify_fd_, &value, sizeof(value));
        (void)ret;  // 忽略返回值
#else
        // 非 Linux 平台：EventFD 不可用，此代码路径不应被执行
        // 如果执行到这里，说明配置处理有问题
        (void)0;  // 空操作
#endif
    }
}

bool LockFreeRingBuffer::wait_for_data(int timeout_ms) {
    if (notify_fd_ < 0) {
        // 通知机制不可用，使用简单轮询
        if (is_next_slot_committed()) {
            return true;
        }
        if (timeout_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return is_next_slot_committed();
        }
        return false;
    }
    
    // 首先快速检查是否有数据
    if (is_next_slot_committed()) {
        // 有数据，进入轮询状态
        auto now = std::chrono::steady_clock::now();
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::POLLING), 
                                       std::memory_order_release);
        metadata_->last_poll_time_ns.store(now_ns, std::memory_order_release);
        return true;
    }
    
    // 检查当前状态
    uint32_t state = metadata_->consumer_state.load(std::memory_order_acquire);
    
    if (state == static_cast<uint32_t>(ConsumerState::POLLING)) {
        // 正在轮询中，检查是否超过30秒
        uint64_t last_poll_time = metadata_->last_poll_time_ns.load(std::memory_order_acquire);
        auto now = std::chrono::steady_clock::now();
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        if (now_ns - last_poll_time < polling_duration_ns_) {
            // 还在轮询期内，继续轮询
            if (timeout_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            }
            return is_next_slot_committed();
        }
        
        // 超过轮询期，切换到等待状态
        metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::WAITING), 
                                       std::memory_order_release);
    }
    
    // 在WAITING状态，等待通知
    if (timeout_ms == 0) {
        return false;
    }
    
    // 根据通知模式选择等待方式
    if (metadata_->notify_mode == NotifyMode::UDS) {
        // UDS 模式：通过 UDS socket 等待通知
        if (wait_via_uds(timeout_ms)) {
            // 收到通知，检查是否有数据
            if (is_next_slot_committed()) {
                // 有数据，进入轮询状态
                auto now = std::chrono::steady_clock::now();
                uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch()).count();
                
                metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::POLLING), 
                                               std::memory_order_release);
                metadata_->last_poll_time_ns.store(now_ns, std::memory_order_release);
                return true;
            }
        }
        // 超时或错误，再次检查是否有数据
        return is_next_slot_committed();
    }
    
    // EventFD 模式（仅 Linux 支持）
#ifdef __linux__
    // Linux: 使用poll等待eventfd
    struct pollfd pfd;
    pfd.fd = notify_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        // 读取eventfd以清除通知
        uint64_t value;
        ssize_t read_ret = read(notify_fd_, &value, sizeof(value));
        (void)read_ret;
        
        // 收到通知，检查是否有数据
        if (is_next_slot_committed()) {
            // 有数据，进入轮询状态
            auto now = std::chrono::steady_clock::now();
            uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
            
            metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::POLLING), 
                                           std::memory_order_release);
            metadata_->last_poll_time_ns.store(now_ns, std::memory_order_release);
            return true;
        }
    }
    
    // 超时或错误，再次检查是否有数据
    return is_next_slot_committed();
#else
    // 非 Linux 平台：EventFD 不可用，此代码路径不应被执行
    // 如果执行到这里，使用简单轮询作为后备
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return is_next_slot_committed();
#endif
}

BufferStats LockFreeRingBuffer::get_stats() const {
    BufferStats stats;
    
    uint64_t write_idx = metadata_->write_index.load(std::memory_order_acquire);
    uint64_t read_idx = metadata_->read_index.load(std::memory_order_acquire);
    
    stats.total_writes = write_idx;
    stats.total_reads = read_idx;
    stats.capacity = metadata_->capacity;
    
    // 计算当前使用的槽位数
    if (write_idx >= read_idx) {
        uint64_t usage = write_idx - read_idx;
        // 限制在容量范围内
        stats.current_usage = (usage > metadata_->capacity) ? metadata_->capacity : usage;
    } else {
        stats.current_usage = 0;
    }
    
    // 丢弃的消息数需要在reserve_slot中跟踪
    // 这里暂时设为0，后续可以添加统计
    stats.dropped_messages = 0;
    
    return stats;
}

// ============================================================================
// UDS 通知机制实现
// ============================================================================

bool LockFreeRingBuffer::init_uds_server(const std::string& path) {
    // 验证路径长度（sockaddr_un.sun_path 最大 108 字节，包含 null 终止符）
    if (path.empty() || path.size() >= 108) {
        return false;
    }
    
    // 创建 SOCK_DGRAM socket（数据报模式，适合简单的通知信号）
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    
    // 删除可能存在的旧 socket 文件
    unlink(path.c_str());
    
    // 设置 socket 地址
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    
    // 绑定到指定路径
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        unlink(path.c_str());
        return false;
    }
    
    // 保存状态
    uds_server_fd_ = fd;
    uds_path_ = path;
    
    return true;
}

bool LockFreeRingBuffer::connect_uds_server(const std::string& path) {
    // 验证路径长度
    if (path.empty() || path.size() >= 108) {
        return false;
    }
    
    // 创建 SOCK_DGRAM socket
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    
    // 设置服务端地址
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    
    // 连接到服务端（对于 SOCK_DGRAM，这设置默认目标地址）
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return false;
    }
    
    // 保存状态
    uds_client_fd_ = fd;
    
    return true;
}

void LockFreeRingBuffer::notify_via_uds() {
    if (uds_client_fd_ < 0) {
        return;
    }
    
    // 发送单字节通知信号（值为 1）
    // 根据 Requirements 7.1：通知机制仅传递一个信号字节，不传递实际日志数据
    uint8_t signal = 1;
    ssize_t ret = send(uds_client_fd_, &signal, sizeof(signal), MSG_DONTWAIT);
    (void)ret;  // 忽略返回值（非阻塞模式下可能失败，但不影响正确性）
}

bool LockFreeRingBuffer::wait_via_uds(int timeout_ms) {
    if (uds_server_fd_ < 0) {
        return false;
    }
    
    // 使用 poll() 等待通知
    struct pollfd pfd;
    pfd.fd = uds_server_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int ret = poll(&pfd, 1, timeout_ms);
    
    if (ret > 0 && (pfd.revents & POLLIN)) {
        // 读取并丢弃通知信号（清空缓冲区）
        uint8_t buffer[64];
        while (recv(uds_server_fd_, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
            // 继续读取直到缓冲区为空
        }
        return true;
    }
    
    // 超时或错误
    return false;
}

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
