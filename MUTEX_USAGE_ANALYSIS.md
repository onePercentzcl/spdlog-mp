# spdlog-mp 项目中 `<mutex>` 的使用分析

## 概述

本项目中 `<mutex>` 主要用于两个方面：
1. **Sink 的线程安全保护**（继承自原版 spdlog）
2. **共享内存注册表的并发访问保护**（多进程扩展新增）

---

## 一、Sink 线程安全保护（原版 spdlog 设计）

### 1.1 设计模式：模板化的 Mutex 策略

spdlog 使用**模板参数**来控制 sink 的线程安全性：

```cpp
template<typename Mutex>
class base_sink {
protected:
    Mutex mutex_;  // 互斥锁
    
    void log(const log_msg& msg) {
        std::lock_guard<Mutex> lock(mutex_);  // 自动加锁
        sink_it_(msg);  // 实际写入操作
    }
};
```

### 1.2 两种版本

| 版本 | Mutex 类型 | 后缀 | 用途 | 性能 |
|------|-----------|------|------|------|
| 多线程版本 | `std::mutex` | `_mt` | 多线程环境 | 有锁开销 |
| 单线程版本 | `details::null_mutex` | `_st` | 单线程环境 | 零开销 |

**null_mutex 实现：**
```cpp
struct null_mutex {
    void lock() {}    // 空操作
    void unlock() {}  // 空操作
};
```

### 1.3 使用示例

```cpp
// 多线程版本 - 使用 std::mutex
using basic_file_sink_mt = basic_file_sink<std::mutex>;

// 单线程版本 - 使用 null_mutex
using basic_file_sink_st = basic_file_sink<details::null_mutex>;
```

### 1.4 所有使用 Mutex 模板的 Sink

**文件 Sink：**
- `basic_file_sink<Mutex>` - 基础文件输出
- `rotating_file_sink<Mutex>` - 轮转文件输出
- `daily_file_sink<Mutex>` - 每日文件输出
- `hourly_file_sink<Mutex>` - 每小时文件输出

**控制台 Sink：**
- `stdout_sink<Mutex>` - 标准输出
- `stderr_sink<Mutex>` - 标准错误输出
- `ansicolor_sink<Mutex>` - 带颜色的控制台输出
- `wincolor_sink<Mutex>` - Windows 控制台颜色

**网络 Sink：**
- `tcp_sink<Mutex>` - TCP 网络输出
- `udp_sink<Mutex>` - UDP 网络输出

**其他 Sink：**
- `ostream_sink<Mutex>` - 标准流输出
- `ringbuffer_sink<Mutex>` - 环形缓冲区
- `callback_sink<Mutex>` - 回调函数
- `dist_sink<Mutex>` - 分发到多个 sink
- `dup_filter_sink<Mutex>` - 重复过滤
- `android_sink<Mutex>` - Android 日志
- `syslog_sink<Mutex>` - Syslog
- `systemd_sink<Mutex>` - Systemd journal
- `msvc_sink<Mutex>` - MSVC 调试器
- `win_eventlog_sink<Mutex>` - Windows 事件日志
- `kafka_sink<Mutex>` - Kafka
- `mongo_sink<Mutex>` - MongoDB

**多进程 Sink（本项目新增）：**
- `SharedMemoryProducerSink<Mutex>` - 共享内存生产者

### 1.5 为什么需要 Mutex？

**场景：多线程同时写日志**

```cpp
// 线程 1
logger->info("Thread 1 message");

// 线程 2
logger->info("Thread 2 message");
```

**没有 Mutex 的问题：**
```
Thread 1: [2024-01-21 10:00:00] Thread 1 mess
Thread 2: [2024-01-21 10:00:00] Thread 2 message
Thread 1: age
```
日志内容交错，文件损坏！

**有 Mutex 的保护：**
```
[2024-01-21 10:00:00] Thread 1 message
[2024-01-21 10:00:00] Thread 2 message
```
日志完整，顺序正确。

---

## 二、共享内存注册表保护（多进程扩展新增）

### 2.1 位置

**文件：** `src/multiprocess/config.cpp`

```cpp
namespace {
    // 全局互斥锁保护注册表操作
    std::mutex registry_mutex;
}
```

### 2.2 作用

保护**共享内存注册表文件**的并发读写。

**注册表文件位置：**
- Linux/macOS: `~/.spdlog/shm_registry.txt`
- Windows: `%LOCALAPPDATA%\spdlog\shm_registry.txt`

**注册表内容：**
```
/spdlog_default_shm
/my_app_shm
/another_shm
```

### 2.3 使用场景

#### 场景 1：注册共享内存

```cpp
void register_shm(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);  // 加锁
    
    std::string path = get_registry_path();
    auto names = read_registry_internal(path);  // 读取现有名称
    names.insert(name);                         // 添加新名称
    write_registry_internal(path, names);       // 写回文件
    
    // lock 析构时自动解锁
}
```

#### 场景 2：注销共享内存

```cpp
void unregister_shm(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);  // 加锁
    
    std::string path = get_registry_path();
    auto names = read_registry_internal(path);  // 读取现有名称
    names.erase(name);                          // 删除名称
    write_registry_internal(path, names);       // 写回文件
    
    // lock 析构时自动解锁
}
```

### 2.4 为什么需要 Mutex？

**问题场景：多个进程同时操作注册表**

```
进程 A: 读取注册表 -> [shm1, shm2]
进程 B: 读取注册表 -> [shm1, shm2]
进程 A: 添加 shm3 -> [shm1, shm2, shm3]
进程 B: 添加 shm4 -> [shm1, shm2, shm4]
进程 A: 写入文件 -> [shm1, shm2, shm3]
进程 B: 写入文件 -> [shm1, shm2, shm4]  ❌ shm3 丢失！
```

**有 Mutex 保护：**
```
进程 A: 加锁 -> 读取 -> 添加 shm3 -> 写入 -> 解锁
进程 B: 等待 -> 加锁 -> 读取 -> 添加 shm4 -> 写入 -> 解锁
结果: [shm1, shm2, shm3, shm4] ✅ 正确！
```

### 2.5 注意事项

⚠️ **这个 Mutex 只保护同一进程内的并发访问！**

**跨进程并发问题：**
- `std::mutex` 是**进程内**的互斥锁
- 不同进程有各自的 `registry_mutex` 实例
- **无法保护跨进程的文件并发访问**

**潜在问题：**
```
进程 A 的 registry_mutex: 加锁 ✓
进程 B 的 registry_mutex: 加锁 ✓  （不同的锁！）
两个进程同时写文件 ❌ 可能冲突
```

**解决方案（未实现）：**
- 使用文件锁（`flock` / `fcntl`）
- 使用命名互斥锁（跨进程）
- 使用原子文件操作

**当前状态：**
- 实际使用中冲突概率很低（注册/注销操作不频繁）
- 可以作为待改进项

---

## 三、其他 Mutex 使用

### 3.1 异步日志队列

**文件：** `include/spdlog/details/mpmc_blocking_q.h`

```cpp
template<typename T>
class mpmc_blocking_queue {
private:
    std::mutex queue_mutex_;              // 保护队列
    std::condition_variable push_cv_;     // 生产者条件变量
    std::condition_variable pop_cv_;      // 消费者条件变量
    circular_q<T> q_;                     // 环形队列
};
```

**作用：** 保护异步日志的消息队列，支持多生产者多消费者（MPMC）。

### 3.2 错误处理

**文件：** `include/spdlog/logger-inl.h`

```cpp
static std::mutex mutex;
static std::chrono::system_clock::time_point last_report_time;
static size_t err_counter = 0;

std::lock_guard<std::mutex> lk{mutex};
// 限制错误报告频率
```

**作用：** 保护错误计数器和时间戳，避免错误日志刷屏。

---

## 四、多进程场景下的线程安全

### 4.1 为什么多进程还需要 Mutex？

**场景：** 多进程 + 多线程

```
进程 A:
  - 线程 1 -> SharedMemoryProducerSink -> 写共享内存
  - 线程 2 -> SharedMemoryProducerSink -> 写共享内存

进程 B:
  - 线程 1 -> SharedMemoryProducerSink -> 写共享内存
  - 线程 2 -> SharedMemoryProducerSink -> 写共享内存
```

**两层保护：**
1. **进程间保护：** 无锁环形缓冲区（lock-free ring buffer）
2. **进程内保护：** `std::mutex`（保护 sink 的 log 方法）

### 4.2 SharedMemoryProducerSink 的 Mutex

```cpp
template<typename Mutex>
class SharedMemoryProducerSink : public base_sink<Mutex> {
    // 继承 base_sink，自动获得 Mutex 保护
};

// 多线程版本
using shared_memory_producer_sink_mt = SharedMemoryProducerSink<std::mutex>;
```

**保护内容：**
- `sink_it_()` 方法的执行
- 确保同一进程内的多个线程不会同时调用 `ring_buffer_->reserve_slot()`

**为什么需要？**
虽然环形缓冲区是无锁的，但 sink 的其他状态（如回退模式检查）需要保护。

---

## 五、总结

### 5.1 Mutex 使用位置

| 位置 | Mutex 类型 | 作用 | 范围 |
|------|-----------|------|------|
| Sink 基类 | 模板参数 | 保护日志写入 | 进程内多线程 |
| 注册表操作 | `std::mutex` | 保护文件读写 | 进程内多线程 |
| 异步队列 | `std::mutex` | 保护消息队列 | 进程内多线程 |
| 错误处理 | `std::mutex` | 保护错误计数 | 进程内多线程 |

### 5.2 设计优势

1. **灵活性：** 模板参数允许用户选择是否需要线程安全
2. **性能：** 单线程场景使用 null_mutex，零开销
3. **一致性：** 所有 sink 使用统一的保护机制

### 5.3 注意事项

⚠️ **`std::mutex` 只保护进程内的并发访问**
- 跨进程并发需要其他机制（无锁算法、文件锁等）
- 本项目的共享内存使用无锁环形缓冲区实现跨进程并发

⚠️ **注册表文件的跨进程并发保护不完善**
- 当前只有进程内保护
- 实际使用中冲突概率很低
- 可作为未来改进项

### 5.4 最佳实践

**选择合适的 Sink 版本：**
```cpp
// 多线程环境 - 使用 _mt 版本
auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt");

// 单线程环境 - 使用 _st 版本（更快）
auto sink = std::make_shared<spdlog::sinks::basic_file_sink_st>("log.txt");

// 多进程多线程 - 使用 _mt 版本
auto sink = std::make_shared<spdlog::multiprocess::shared_memory_producer_sink_mt>(handle);
```

---

## 六、相关文件清单

### 6.1 核心文件

- `include/spdlog/sinks/base_sink.h` - Sink 基类，定义 Mutex 模板
- `include/spdlog/sinks/base_sink-inl.h` - Sink 基类实现
- `src/multiprocess/config.cpp` - 注册表 Mutex

### 6.2 所有使用 Mutex 的 Sink

**文件 Sink：**
- `include/spdlog/sinks/basic_file_sink.h`
- `include/spdlog/sinks/rotating_file_sink.h`
- `include/spdlog/sinks/daily_file_sink.h`
- `include/spdlog/sinks/hourly_file_sink.h`

**控制台 Sink：**
- `include/spdlog/sinks/ansicolor_sink.h`
- `include/spdlog/sinks/wincolor_sink.h`

**网络 Sink：**
- `include/spdlog/sinks/tcp_sink.h`
- `include/spdlog/sinks/udp_sink.h`

**其他 Sink：**
- `include/spdlog/sinks/ostream_sink.h`
- `include/spdlog/sinks/ringbuffer_sink.h`
- `include/spdlog/sinks/callback_sink.h`
- `include/spdlog/sinks/dist_sink.h`
- `include/spdlog/sinks/dup_filter_sink.h`
- `include/spdlog/sinks/null_sink.h`
- 等等...

**多进程 Sink：**
- `include/spdlog/multiprocess/shared_memory_producer_sink.h`

### 6.3 其他使用 Mutex 的组件

- `include/spdlog/details/mpmc_blocking_q.h` - 异步队列
- `include/spdlog/details/registry.h` - Logger 注册表
- `include/spdlog/details/periodic_worker.h` - 周期性工作线程
- `include/spdlog/logger-inl.h` - 错误处理

---

**文档版本：** v1.0  
**创建时间：** 2026年1月21日  
**作者：** spdlog-mp 项目
