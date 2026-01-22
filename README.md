# spdlog-mp

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)]()
[![Version](https://img.shields.io/badge/version-1.0.6-blue.svg)]()

基于 [spdlog](https://github.com/gabime/spdlog) 的多进程共享内存日志扩展。

> **项目说明**
> - 本项目基于 [spdlog v1.x](https://github.com/gabime/spdlog) 开发
> - 多进程共享内存功能由 [Kiro](https://kiro.dev) AI 辅助生成
> - 保持与原版 spdlog 的完全兼容性

## 为什么需要这个项目？

### 问题背景

在多进程应用程序中（如机器人系统、分布式服务、微服务架构），日志管理面临以下挑战：

1. **日志分散** - 每个进程独立写日志文件，导致日志分散在多个文件中，难以追踪问题
2. **时序混乱** - 不同进程的日志时间戳不同步，难以还原事件发生的真实顺序
3. **资源竞争** - 多进程同时写入同一文件会导致锁竞争，严重影响性能
4. **文件句柄限制** - 大量进程各自打开日志文件，消耗系统文件句柄资源

### 解决方案

本项目通过**共享内存 + 无锁环形缓冲区**的架构解决上述问题：

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  进程 1     │     │  进程 2     │     │  进程 N     │
│ (生产者)    │     │ (生产者)    │     │ (生产者)    │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       └───────────────────┼───────────────────┘
                           │
                           ▼
              ┌────────────────────────┐
              │     共享内存           │
              │  (无锁环形缓冲区)      │
              │   MPSC Queue          │
              └───────────┬────────────┘
                          │
                          ▼
              ┌────────────────────────┐
              │      主进程            │
              │     (消费者)           │
              │  ┌─────────────────┐   │
              │  │ 控制台输出      │   │
              │  │ 文件写入        │   │
              │  │ 日志轮转        │   │
              │  └─────────────────┘   │
              └────────────────────────┘
```

### 核心优势

| 问题 | 传统方案 | 本项目方案 |
|------|----------|------------|
| 日志分散 | 每个进程独立文件 | 统一输出到单一文件 |
| 时序混乱 | 各自时间戳 | 统一时间戳，保持顺序 |
| 资源竞争 | 文件锁/互斥锁 | 无锁队列，零竞争 |
| 性能开销 | 频繁 I/O | 内存写入，批量 I/O |
| API 复杂度 | 需要额外配置 | 一行代码启用 |

### 适用场景

- **机器人系统** - 主控进程 + 多个传感器/执行器进程
- **微服务架构** - 多个服务进程共享日志收集
- **Fork 多进程** - 父进程 fork 出多个工作进程
- **分布式计算** - 多进程并行计算任务

## 特性

- **简洁API** - 一行代码启用多进程日志
- **零配置** - 支持无参数调用，使用合理的默认值
- **高性能** - 无锁环形缓冲区（MPSC），支持高并发写入
- **日志轮转** - 自动按大小切换日志文件
- **灵活配置** - 支持自定义共享内存、日志路径、轮转策略等
- **跨平台** - 支持 macOS、Linux、Windows

## 安装

### 使用 xmake（推荐）

```lua
-- xmake.lua
add_requires("spdlog-mp")

target("myapp")
    set_kind("binary")
    add_packages("spdlog-mp")
```

### 使用 CMake

```bash
git clone https://github.com/peterzheng98/spdlog-mp.git
cd spdlog-mp
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON
cmake --build build
sudo cmake --install build
```

### 手动编译

```bash
# xmake
xmake f --multiprocess=y
xmake

# cmake
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON
cmake --build build
```

## 快速开始

### 最简单用法

```cpp
#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

int main() {
    // 消费者（主进程）- 一行代码启用，自动启动
    spdlog::EnableConsumer();
    
    spdlog::SetProcessName("Main");
    spdlog::info("Hello from main process!");
    
    if (fork() == 0) {
        // 生产者（子进程）
        spdlog::EnableProducer(spdlog::ProducerConfig(spdlog::GetSharedMemoryHandle()));
        spdlog::SetProcessName("Son1");
        spdlog::info("Hello from child process!");
        _exit(0);
    }
    
    wait(nullptr);
    spdlog::Shutdown();
    return 0;
}
```

### 默认配置

无参数调用时使用以下默认值：

| 配置项 | 默认值 |
|--------|--------|
| 共享内存名称 | `/spdlog_default_shm` |
| 共享内存大小 | 4MB |
| 日志目录 | `logs/` |
| 日志名称 | `app` |
| 日志文件 | `logs/YYYYMMDD_HHMMSS_app.log` |
| 日志轮转 | 启用，单文件10MB，最多5个 |
| 轮询间隔 (poll_interval) | 10ms |
| 轮询持续时间 (poll_duration) | 1000ms (1秒) |
| 槽位大小 | 4096字节 |
| 通知模式 (notify_mode) | UDS (Unix Domain Socket) |

**通知模式说明：**
- `UDS` (默认): Unix Domain Socket，跨平台支持 (macOS/Linux)
- `EventFD`: Linux 专用，适用于 fork 场景，性能更高

**性能优化说明：**
- `poll_interval`: 消费者线程检查新消息的间隔
- `poll_duration`: POLLING 状态持续时间。消费者收到消息后进入 POLLING 状态，在此期间生产者跳过 eventfd/kqueue 通知，减少系统调用开销

## API 参考

### EnableConsumer

启用消费者模式（主进程）。创建共享内存并自动启动消费者线程。

```cpp
// 无参数 - 使用默认配置，自动启动
spdlog::EnableConsumer();

// 使用配置结构体
spdlog::ConsumerConfig cfg;
cfg.shm_name = "/my_shm";
cfg.log_name = "myapp";
cfg.max_file_size = 20 * 1024 * 1024;  // 20MB
cfg.poll_duration = std::chrono::milliseconds(2000);  // POLLING 状态持续 2 秒
cfg.poll_interval = std::chrono::milliseconds(10);    // 轮询间隔 10ms
cfg.notify_mode = spdlog::NotifyMode::UDS;            // 通知模式（默认 UDS）
spdlog::EnableConsumer(cfg);
```

### EnableProducer

启用生产者模式（子进程）。

```cpp
// fork场景 - 使用父进程句柄
auto handle = spdlog::GetSharedMemoryHandle();
if (fork() == 0) {
    spdlog::EnableProducer(spdlog::ProducerConfig(handle));
}

// 独立进程场景 - 通过名称连接
spdlog::EnableProducer(spdlog::ProducerConfig("/my_shm"));

// 无参数 - 使用默认共享内存名称
spdlog::EnableProducer();
```

### 其他API

```cpp
// 设置进程名（4字符，不足补空格）
spdlog::SetProcessName("Main");

// 设置模块名（6字符，居中显示）
spdlog::SetModuleName("Vision");

// 获取共享内存句柄（供fork子进程使用）
auto handle = spdlog::GetSharedMemoryHandle();

// 关闭日志系统
spdlog::Shutdown();
```

详细 API 文档请参考 [include/spdlog/multiprocess/README.md](include/spdlog/multiprocess/README.md)。

## 使用场景

### 场景1：Fork子进程

```cpp
spdlog::EnableConsumer();

auto handle = spdlog::GetSharedMemoryHandle();

for (int i = 0; i < 3; ++i) {
    if (fork() == 0) {
        spdlog::EnableProducer(spdlog::ProducerConfig(handle));
        spdlog::SetProcessName("Son" + std::to_string(i));
        spdlog::info("Child {} started", i);
        _exit(0);
    }
}
```

### 场景2：独立进程

```bash
# 终端1：启动消费者
./app consumer

# 终端2：启动生产者
./app producer
```

```cpp
// 消费者
spdlog::ConsumerConfig cfg;
cfg.shm_name = "/my_app_shm";
spdlog::EnableConsumer(cfg);

// 生产者
spdlog::EnableProducer(spdlog::ProducerConfig("/my_app_shm"));
```

### 场景3：使用已存在的共享内存

```cpp
// 用户自己创建共享内存
int fd = shm_open("/user_shm", O_CREAT | O_RDWR, 0666);
ftruncate(fd, 8 * 1024 * 1024);  // 8MB

// 使用其中一部分区域
spdlog::ConsumerConfig cfg;
cfg.shm_name = "/user_shm";
cfg.shm_size = 8 * 1024 * 1024;
cfg.create_shm = false;          // 连接已存在的
cfg.shm_offset = 1024 * 1024;    // 从1MB处开始
spdlog::EnableConsumer(cfg);
```

## 日志格式

### 标准格式（默认）

```
控制台：[HH:MM:SS.mmm] [level] [logger_name] 消息
文件：[YYYY-MM-DD HH:MM:SS.ffffff] [level] [logger_name] 消息
```

### OnePet格式（enable_onep_format=true）

```
Debug控制台：[HH:MM:SS.mmm] [LEVEL] [进程名:PID] [模块名:ThreadID] 消息
Release控制台：[HH:MM:SS] [LEVEL] [进程名] [模块名] 消息
文件：[YYYY-MM-DD HH:MM:SS.ffffff] [LEVEL] [进程名:PID] [模块名:ThreadID] 消息
```

## 性能

基于无锁环形缓冲区（MPSC）实现，以下为实际基准测试结果。

### macOS (Apple Silicon M1)

测试环境：macOS, Apple M1, 8GB RAM, UDS 通知模式

| 测试场景 | 吞吐量 | 说明 |
|----------|--------|------|
| 单线程写入 | **7.7M msg/s** | 100K 消息，100字节/条 |
| 多线程写入 (4线程) | **2.5M msg/s** | 400K 消息总计 |
| 多进程写入 (2进程) | **7.1M msg/s** | 200K 消息总计 |

### 写入延迟 (macOS)

| 百分位 | 延迟 |
|--------|------|
| P50 | 0.21 μs |
| P90 | 0.54 μs |
| P99 | 2.83 μs |
| P99.9 | 5.71 μs |
| Max | 22.54 μs |

### 不同消息长度性能 (macOS)

| 消息长度 | 吞吐量 (msg/s) | 吞吐量 (MB/s) |
|----------|----------------|---------------|
| 50 字节 | 3.3M | 160 |
| 100 字节 | 2.9M | 278 |
| 200 字节 | 3.7M | 714 |
| 500 字节 | 2.0M | 938 |
| 800 字节 | 2.4M | 1818 |

### 不同线程数性能 (macOS)

| 线程数 | 总消息数 | 吞吐量 (msg/s) |
|--------|----------|----------------|
| 1 | 25K | 3.5M |
| 2 | 50K | 2.1M |
| 4 | 100K | 1.5M |
| 8 | 200K | 2.3M |

### 运行性能测试

```bash
# xmake
xmake f --multiprocess=y
xmake
xmake run example_mp3           # 完整测试
xmake run example_mp3 --quick   # 快速测试
xmake run example_mp3 --integrity  # 消息完整性验证

# cmake
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON -DSPDLOG_BUILD_EXAMPLE=ON
cmake --build build
./build/example/example_mp3
```

### 运行基准测试

```bash
# xmake
xmake f --multiprocess=y --build_bench=y
xmake
xmake run multiprocess_bench

# cmake
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON -DSPDLOG_BUILD_BENCH=ON
cmake --build build
./build/bench/multiprocess_bench
```

## 示例程序

```bash
# 编译示例
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON -DSPDLOG_BUILD_EXAMPLE=ON
cmake --build build

# 运行示例
./build/example/multiprocess_default_example   # 默认配置示例
./build/example/multiprocess_fork_example      # Fork多子进程示例
./build/example/multiprocess_separate_example  # 独立进程示例
./build/example/multiprocess_offset_example    # 共享内存偏移量示例
```

## 原版 spdlog 功能

本项目完全兼容原版 spdlog 的所有功能，包括：

- 同步/异步日志
- 多种 sink（控制台、文件、轮转文件、syslog 等）
- 自定义格式化
- 日志级别过滤
- Backtrace 支持
- 等等...

详细文档请参考 [spdlog wiki](https://github.com/gabime/spdlog/wiki)。

## 许可证

MIT License - 与原版 spdlog 相同。

## 致谢

- [spdlog](https://github.com/gabime/spdlog) - 优秀的 C++ 日志库
- [Kiro](https://kiro.dev) - AI 辅助开发工具
