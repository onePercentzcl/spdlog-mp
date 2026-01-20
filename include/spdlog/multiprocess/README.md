# spdlog 多进程共享内存日志模块

基于共享内存的高性能多进程日志解决方案。

## 特性

- **简洁API** - 一行代码启用多进程日志
- **零配置** - 支持无参数调用，使用合理的默认值
- **高性能** - 无锁环形缓冲区，支持高并发写入
- **日志轮转** - 自动按大小切换日志文件
- **灵活配置** - 支持自定义共享内存、日志路径、轮转策略等

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
| 轮询间隔 | 10ms |
| 轮询时间 | 1s |
| 槽位大小 | 4096字节 |
| enable_onep_format | false（使用标准格式） |

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
spdlog::EnableConsumer(cfg);
```

#### ConsumerConfig 配置项

```cpp
struct ConsumerConfig {
    // ========== 共享内存配置 ==========
    std::string shm_name = "/spdlog_default_shm";   // 共享内存名称
    size_t shm_size = 4 * 1024 * 1024;              // 共享内存大小（4MB）
    bool create_shm = true;                         // true=创建新共享内存，false=连接已存在的
    size_t shm_offset = 0;                          // 偏移量（⚠️ 仅 create_shm=false 时有效）
    
    // ========== 日志输出配置 ==========
    std::string log_dir = "logs/";                  // 日志目录
    std::string log_name = "app";                   // 日志名称（生成文件名：YYYYMMDD_HHMMSS_name.log）
    std::string log_file;                           // 完整路径（⚠️ 设置后忽略 log_dir 和 log_name）
    
    // ========== 日志轮转配置 ==========
    bool enable_rotating = true;                    // 是否启用轮转
    size_t max_file_size = 10 * 1024 * 1024;        // 单文件最大10MB（⚠️ 仅 enable_rotating=true 时有效）
    size_t max_files = 5;                           // 最多保留文件数（⚠️ 仅 enable_rotating=true 时有效）
    
    // ========== 缓冲区配置 ==========
    size_t slot_size = 4096;                        // 槽位大小（字节）
    
    // ========== 溢出策略配置 ==========
    OverflowPolicy overflow_policy = OverflowPolicy::Block;  // Block=阻塞等待，Drop=丢弃
    std::chrono::milliseconds block_timeout{5000};           // 阻塞超时（⚠️ 仅 overflow_policy=Block 时有效）
    
    // ========== 轮询配置 ==========
    std::chrono::milliseconds poll_interval{10};    // 轮询间隔
    std::chrono::milliseconds poll_duration{1000};  // 轮询持续时间
    
    // ========== 模式配置 ==========
    bool async_mode = false;                        // 是否启用异步模式
    bool enable_onep_format = false;                // 是否启用OnePet格式（默认false使用标准格式）
};
```

#### 配置项互斥关系

| 配置项 | 互斥/依赖关系 |
|--------|--------------|
| `shm_offset` | 仅当 `create_shm=false` 时有效 |
| `log_file` | 设置后忽略 `log_dir` 和 `log_name` |
| `max_file_size` | 仅当 `enable_rotating=true` 时有效 |
| `max_files` | 仅当 `enable_rotating=true` 时有效 |
| `block_timeout` | 仅当 `overflow_policy=Block` 时有效 |

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

#### ProducerConfig 配置项

```cpp
struct ProducerConfig {
    // ========== 共享内存配置（二选一）==========
    SharedMemoryHandle shm_handle;                  // fork场景：使用父进程句柄（⚠️ 与 shm_name 互斥）
    std::string shm_name = "/spdlog_default_shm";   // 独立进程场景：共享内存名称（⚠️ 与 shm_handle 互斥）
    size_t shm_size = 4 * 1024 * 1024;              // 共享内存大小（⚠️ 仅独立进程场景有效）
    size_t shm_offset = 0;                          // 共享内存偏移量
    
    // ========== 缓冲区配置 ==========
    size_t slot_size = 4096;                        // 槽位大小（字节）
    
    // ========== 溢出策略配置 ==========
    OverflowPolicy overflow_policy = OverflowPolicy::Block;  // Block=阻塞等待，Drop=丢弃
    std::chrono::milliseconds block_timeout{5000};           // 阻塞超时（⚠️ 仅 overflow_policy=Block 时有效）
    
    // ========== 模式配置 ==========
    bool async_mode = false;                        // 是否启用异步模式
    bool enable_onep_format = false;                // 是否启用OnePet格式
};
```

#### 配置项互斥关系

| 配置项 | 互斥/依赖关系 |
|--------|--------------|
| `shm_handle` | 与 `shm_name` 互斥，fork场景使用 |
| `shm_name` | 与 `shm_handle` 互斥，独立进程场景使用 |
| `shm_size` | 仅当使用 `shm_name`（独立进程场景）时有效 |
| `block_timeout` | 仅当 `overflow_policy=Block` 时有效 |

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

### 标准格式（enable_onep_format=false，默认）

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

### 日志级别

| 级别 | 显示 | 颜色 |
|------|------|------|
| trace | TRAC | 默认 |
| debug | DBUG | 青色 |
| info | INFO | 绿色 |
| warn | WARN | 黄色 |
| error | ERRO | 红色 |
| critical | CRIT | 紫色 |

## 编译

```bash
cmake -B build -DSPDLOG_ENABLE_MULTIPROCESS=ON
cmake --build build
```

## 示例程序

- `multiprocess_default_example` - 默认配置示例
- `multiprocess_fork_example` - Fork多子进程示例
- `multiprocess_separate_example` - 独立进程示例
- `multiprocess_offset_example` - 共享内存偏移量示例

```bash
# 运行示例
./build/example/multiprocess_default_example
./build/example/multiprocess_fork_example
```
