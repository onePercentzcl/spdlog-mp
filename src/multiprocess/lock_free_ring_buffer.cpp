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
#elif defined(__APPLE__) || defined(__FreeBSD__)
// macOS: use kqueue for notification
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace spdlog {

// 消费者状态枚举定义
enum class ConsumerState : uint32_t {
    WAITING = 0,    // 等待通知状态
    POLLING = 1     // 轮询状态（30秒内不需要通知）
};

LockFreeRingBuffer::LockFreeRingBuffer(void* memory, size_t total_size, size_t slot_size, 
                                       OverflowPolicy overflow_policy, bool initialize)
    : metadata_(nullptr), slots_base_(nullptr), slot_count_(0), slot_size_(slot_size), eventfd_(-1) {
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
        
        // 创建通知机制
#ifdef __linux__
        // Linux: 使用eventfd
        eventfd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
        if (eventfd_ >= 0) {
            metadata_->eventfd = eventfd_;
        } else {
            metadata_->eventfd = -1;
        }
#elif defined(__APPLE__) || defined(__FreeBSD__)
        // macOS: 使用kqueue
        eventfd_ = kqueue();
        if (eventfd_ >= 0) {
            metadata_->eventfd = eventfd_;
        } else {
            metadata_->eventfd = -1;
        }
#else
        // 其他平台：不支持通知
        metadata_->eventfd = -1;
        eventfd_ = -1;
#endif
        
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
        // 生产者：从元数据读取eventfd/kqueue
        eventfd_ = metadata_->eventfd;
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
    if (eventfd_ < 0) {
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
        
        if (now_ns - last_poll_time < POLLING_DURATION_NS) {
            // 还在30秒轮询期内，不需要通知
            return;
        }
        // 超过30秒了，消费者可能已经进入等待状态，继续发送通知
    }
    
    // 消费者在WAITING状态，或者轮询期已过，发送通知
#ifdef __linux__
    // Linux: 写入eventfd
    uint64_t value = 1;
    ssize_t ret = write(eventfd_, &value, sizeof(value));
    (void)ret;  // 忽略返回值
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // macOS: 使用kqueue触发事件
    // 使用用户自定义事件
    struct kevent kev;
    EV_SET(&kev, 1, EVFILT_USER, EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_TRIGGER, 0, NULL);
    kevent(eventfd_, &kev, 1, NULL, 0, NULL);
#endif
}

bool LockFreeRingBuffer::wait_for_data(int timeout_ms) {
    if (eventfd_ < 0) {
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
        
        if (now_ns - last_poll_time < POLLING_DURATION_NS) {
            // 还在30秒内，继续轮询
            if (timeout_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            }
            return is_next_slot_committed();
        }
        
        // 超过30秒，切换到等待状态
        metadata_->consumer_state.store(static_cast<uint32_t>(ConsumerState::WAITING), 
                                       std::memory_order_release);
    }
    
    // 在WAITING状态，等待通知
    if (timeout_ms == 0) {
        return false;
    }
    
#ifdef __linux__
    // Linux: 使用poll等待eventfd
    struct pollfd pfd;
    pfd.fd = eventfd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        // 读取eventfd以清除通知
        uint64_t value;
        ssize_t read_ret = read(eventfd_, &value, sizeof(value));
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
    
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // macOS: 使用kqueue等待事件
    struct kevent kev;
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    
    // 注册用户事件
    EV_SET(&kev, 1, EVFILT_USER, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
    kevent(eventfd_, &kev, 1, NULL, 0, NULL);
    
    // 等待事件
    struct kevent event;
    int ret = kevent(eventfd_, NULL, 0, &event, 1, &ts);
    
    if (ret > 0) {
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
    // 其他平台：简单轮询
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

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
