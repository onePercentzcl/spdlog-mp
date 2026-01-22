// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

/**
 * @file custom_formatter.h
 * @brief OnePet日志格式化器 - Header Only实现
 * 
 * 简洁API：
 * - EnableConsumer()   - 启用消费者模式（主进程，自动启动）
 * - EnableProducer()   - 启用生产者模式（子进程）
 * - EnableOnepFormat() - 启用单进程模式
 * - SetProcessName()   - 设置进程名
 * - SetModuleName()    - 设置模块名
 * - Shutdown()         - 关闭日志系统
 * 
 * 使用示例：
 * @code
 * // 消费者（主进程）- 自动启动
 * spdlog::EnableConsumer();
 * 
 * // 生产者（子进程）
 * spdlog::EnableProducer(shm_handle, "Child");
 * 
 * // 单进程模式
 * spdlog::EnableOnepFormat("Main", "logs/app.log");
 * @endcode
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/null_sink.h>

#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// 多进程支持（可选）
#ifdef SPDLOG_ENABLE_MULTIPROCESS
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace spdlog {

// ============================================================================
// 内部实现
// ============================================================================
namespace detail {

// 全局状态
struct OnepState {
    std::string process_name = "";  // 默认空，显示NULL
    std::unordered_map<size_t, std::string> module_names;
    std::mutex module_mutex;
    bool initialized = false;
    bool async_mode = false;
    pid_t creator_pid = 0;  // 记录创建状态的进程ID，用于检测fork
    
#ifdef SPDLOG_ENABLE_MULTIPROCESS
    SharedMemoryHandle shm_handle;
    std::shared_ptr<multiprocess::SharedMemoryConsumerSink> consumer_sink;
#endif
    
    static OnepState& instance() {
        static OnepState s;
        return s;
    }
    
    // 检测当前进程是否是fork的子进程
    bool is_forked_child() const {
        return creator_pid != 0 && creator_pid != getpid();
    }
};

// 日志级别格式化器（带颜色）
class LevelFormatterColored : public custom_flag_formatter {
public:
    void format(const details::log_msg &msg, const std::tm &, memory_buf_t &dest) override {
        static const char* levels[] = {"TRAC", "DBUG", "INFO", "WARN", "ERRO", "CRIT", "OFF "};
        static const char* colors[] = {
            "\033[1;0m", "\033[1;36m", "\033[1;32m", 
            "\033[1;33m", "\033[1;91m", "\033[1;95m", ""
        };
        int idx = static_cast<int>(msg.level);
        if (idx >= 0 && idx < 7) {
            dest.append(colors[idx], colors[idx] + strlen(colors[idx]));
            dest.append(levels[idx], levels[idx] + 4);
            const char* reset = "\033[0m";
            dest.append(reset, reset + 4);
        }
    }
    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<LevelFormatterColored>();
    }
};

// 日志级别格式化器（无颜色）
class LevelFormatter : public custom_flag_formatter {
public:
    void format(const details::log_msg &msg, const std::tm &, memory_buf_t &dest) override {
        static const char* levels[] = {"TRAC", "DBUG", "INFO", "WARN", "ERRO", "CRIT", "OFF "};
        int idx = static_cast<int>(msg.level);
        if (idx >= 0 && idx < 7) {
            dest.append(levels[idx], levels[idx] + 4);
        }
    }
    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<LevelFormatter>();
    }
};

// 进程名格式化器
class ProcessNameFormatter : public custom_flag_formatter {
public:
    void format(const details::log_msg &, const std::tm &, memory_buf_t &dest) override {
        std::string name = OnepState::instance().process_name;
        if (name.empty()) name = "NULL";
        if (name.length() > 4) name = name.substr(0, 4);
        while (name.length() < 4) name += ' ';
        dest.append(name.data(), name.data() + 4);
    }
    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<ProcessNameFormatter>();
    }
};

// 模块名格式化器（带ThreadID）
class ModuleWithThreadFormatter : public custom_flag_formatter {
public:
    void format(const details::log_msg &msg, const std::tm &, memory_buf_t &dest) override {
        auto& state = OnepState::instance();
        std::string name;
        {
            std::lock_guard<std::mutex> lock(state.module_mutex);
            auto it = state.module_names.find(msg.thread_id);
            name = (it != state.module_names.end()) ? it->second : "";
        }
        if (name.empty()) name = "NULL";
        if (name.length() > 6) name = name.substr(0, 6);
        else if (name.length() < 6) {
            size_t pad = 6 - name.length();
            name = std::string(pad / 2, ' ') + name + std::string(pad - pad / 2, ' ');
        }
        std::string out = " [" + name + ":" + std::to_string(msg.thread_id) + "]";
        dest.append(out.data(), out.data() + out.size());
    }
    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<ModuleWithThreadFormatter>();
    }
};

// 模块名格式化器（无ThreadID）
class ModuleFormatter : public custom_flag_formatter {
public:
    void format(const details::log_msg &msg, const std::tm &, memory_buf_t &dest) override {
        auto& state = OnepState::instance();
        std::string name;
        {
            std::lock_guard<std::mutex> lock(state.module_mutex);
            auto it = state.module_names.find(msg.thread_id);
            name = (it != state.module_names.end()) ? it->second : "";
        }
        if (name.empty()) return;
        if (name.length() > 6) name = name.substr(0, 6);
        else if (name.length() < 6) {
            size_t pad = 6 - name.length();
            name = std::string(pad / 2, ' ') + name + std::string(pad - pad / 2, ' ');
        }
        std::string out = " [" + name + "]";
        dest.append(out.data(), out.data() + out.size());
    }
    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<ModuleFormatter>();
    }
};

// 创建控制台Sink
inline std::shared_ptr<sinks::stdout_color_sink_mt> CreateConsoleSink(level::level_enum lvl) {
    auto sink = std::make_shared<sinks::stdout_color_sink_mt>();
    sink->set_color_mode(color_mode::always);
    sink->set_color(level::trace,    "\033[1;0m");
    sink->set_color(level::debug,    "\033[1;36m");
    sink->set_color(level::info,     "\033[1;32m");
    sink->set_color(level::warn,     "\033[1;33m");
    sink->set_color(level::err,      "\033[1;91m");
    sink->set_color(level::critical, "\033[1;95m");
    
    auto fmt = std::make_unique<pattern_formatter>();
    fmt->add_flag<LevelFormatterColored>('*');
    fmt->add_flag<ProcessNameFormatter>('p');
    fmt->add_flag<ModuleWithThreadFormatter>('d');
    fmt->add_flag<ModuleFormatter>('r');
#ifdef NDEBUG
    fmt->set_pattern("[%H:%M:%S] [%*] [%p:%P]%r %v");
#else
    fmt->set_pattern("[%H:%M:%S.%e] [%*] [%p:%P]%d %v");
#endif
    sink->set_formatter(std::move(fmt));
    sink->set_level(lvl);
    return sink;
}

// 创建文件Sink
inline std::shared_ptr<sinks::basic_file_sink_mt> CreateFileSink(
    const std::string& filename, bool truncate, level::level_enum lvl) {
    auto sink = std::make_shared<sinks::basic_file_sink_mt>(filename, truncate);
    auto fmt = std::make_unique<pattern_formatter>();
    fmt->add_flag<LevelFormatter>('*');
    fmt->add_flag<ProcessNameFormatter>('p');
    fmt->add_flag<ModuleWithThreadFormatter>('d');
    fmt->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%*] [%p:%P]%d %v");
    sink->set_formatter(std::move(fmt));
    sink->set_level(lvl);
    return sink;
}

// 消费者专用控制台Sink
inline std::shared_ptr<sinks::stdout_color_sink_mt> CreateConsumerConsoleSink() {
    auto sink = std::make_shared<sinks::stdout_color_sink_mt>();
    sink->set_color_mode(color_mode::always);
    sink->set_color(level::trace,    "\033[1;0m");
    sink->set_color(level::debug,    "\033[1;36m");
    sink->set_color(level::info,     "\033[1;32m");
    sink->set_color(level::warn,     "\033[1;33m");
    sink->set_color(level::err,      "\033[1;91m");
    sink->set_color(level::critical, "\033[1;95m");
    auto fmt = std::make_unique<pattern_formatter>();
    fmt->add_flag<LevelFormatterColored>('*');
#ifdef NDEBUG
    // Release 模式：简洁格式，不显示毫秒
    fmt->set_pattern("[%H:%M:%S] [%*] [%n] %v");
#else
    // Debug 模式：详细格式，显示毫秒
    fmt->set_pattern("[%H:%M:%S.%e] [%*] [%n] %v");
#endif
    sink->set_formatter(std::move(fmt));
    sink->set_level(level::trace);
    return sink;
}

// 消费者专用文件Sink
inline std::shared_ptr<sinks::basic_file_sink_mt> CreateConsumerFileSink(
    const std::string& filename, bool truncate = true) {
    auto sink = std::make_shared<sinks::basic_file_sink_mt>(filename, truncate);
    auto fmt = std::make_unique<pattern_formatter>();
    fmt->add_flag<LevelFormatter>('*');
    fmt->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%*] [%n] %v");
    sink->set_formatter(std::move(fmt));
    sink->set_level(level::trace);
    return sink;
}

// 消费者专用轮转文件Sink
inline std::shared_ptr<sinks::rotating_file_sink_mt> CreateConsumerRotatingFileSink(
    const std::string& filename, size_t max_size, size_t max_files) {
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(filename, max_size, max_files);
    auto fmt = std::make_unique<pattern_formatter>();
    fmt->add_flag<LevelFormatter>('*');
    fmt->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%*] [%n] %v");
    sink->set_formatter(std::move(fmt));
    sink->set_level(level::trace);
    return sink;
}

// 标准控制台Sink（不使用OnePet格式）
inline std::shared_ptr<sinks::stdout_color_sink_mt> CreateStandardConsoleSink() {
    auto sink = std::make_shared<sinks::stdout_color_sink_mt>();
    sink->set_color_mode(color_mode::always);
    sink->set_color(level::trace,    "\033[1;0m");
    sink->set_color(level::debug,    "\033[1;36m");
    sink->set_color(level::info,     "\033[1;32m");
    sink->set_color(level::warn,     "\033[1;33m");
    sink->set_color(level::err,      "\033[1;91m");
    sink->set_color(level::critical, "\033[1;95m");
    // 标准格式：[时间] [级别] [logger名] 消息
    sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
    sink->set_level(level::trace);
    return sink;
}

// 标准文件Sink（不使用OnePet格式）
inline std::shared_ptr<sinks::basic_file_sink_mt> CreateStandardFileSink(
    const std::string& filename, bool truncate = true) {
    auto sink = std::make_shared<sinks::basic_file_sink_mt>(filename, truncate);
    // 标准格式：[时间] [级别] [logger名] 消息
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] [%n] %v");
    sink->set_level(level::trace);
    return sink;
}

// 标准轮转文件Sink（不使用OnePet格式）
inline std::shared_ptr<sinks::rotating_file_sink_mt> CreateStandardRotatingFileSink(
    const std::string& filename, size_t max_size, size_t max_files) {
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(filename, max_size, max_files);
    // 标准格式：[时间] [级别] [logger名] 消息
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] [%n] %v");
    sink->set_level(level::trace);
    return sink;
}

// 生成日志文件名：[log_dir]/YYYYMMDD_HHMMSS_[name].log
// @param log_dir 日志目录（默认 "logs/"）
// @param name 日志名称（默认 "app"）
inline std::string GenerateLogFilename(const std::string& log_dir = "logs/", 
                                        const std::string& name = "app") {
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif
    
    // 格式化文件名：YYYYMMDD_HHMMSS_name.log
    std::ostringstream oss;
    oss << log_dir
        << std::setfill('0') << std::setw(4) << (tm_now.tm_year + 1900)
        << std::setw(2) << (tm_now.tm_mon + 1)
        << std::setw(2) << tm_now.tm_mday
        << "_"
        << std::setw(2) << tm_now.tm_hour
        << std::setw(2) << tm_now.tm_min
        << std::setw(2) << tm_now.tm_sec
        << "_"
        << name
        << ".log";
    
    return oss.str();
}

// 默认共享内存名称
inline const char* GetDefaultShmName() {
    return "/spdlog_default_shm";
}

} // namespace detail

// ============================================================================
// 公共API
// ============================================================================

/**
 * @brief 设置进程名
 */
inline void SetProcessName(const std::string& name) {
    detail::OnepState::instance().process_name = name;
}

/**
 * @brief 获取进程名
 */
inline std::string GetProcessName() {
    return detail::OnepState::instance().process_name;
}

/**
 * @brief 设置当前线程的模块名
 */
inline void SetModuleName(const std::string& name) {
    auto& state = detail::OnepState::instance();
    std::lock_guard<std::mutex> lock(state.module_mutex);
    state.module_names[details::os::thread_id()] = name;
}

/**
 * @brief 获取模块名
 */
inline std::string GetModuleName(size_t thread_id = 0) {
    if (thread_id == 0) thread_id = details::os::thread_id();
    auto& state = detail::OnepState::instance();
    std::lock_guard<std::mutex> lock(state.module_mutex);
    auto it = state.module_names.find(thread_id);
    return (it != state.module_names.end()) ? it->second : "";
}

/**
 * @brief 启用OnePet格式（单进程模式）
 * 
 * @param process_name 进程名
 * @param log_file 日志文件（空则不输出到文件）
 * @param console_level 控制台日志级别
 * @param async_mode 是否启用异步模式
 */
inline void EnableOnepFormat(
    const std::string& process_name = "Main",
    const std::string& log_file = "",
    level::level_enum console_level = level::info,
    bool async_mode = false) {
    
    SetProcessName(process_name);
    auto& state = detail::OnepState::instance();
    state.async_mode = async_mode;
    
    std::vector<sink_ptr> sinks;
    sinks.push_back(detail::CreateConsoleSink(console_level));
    if (!log_file.empty()) {
        sinks.push_back(detail::CreateFileSink(log_file, true, level::trace));
    }
    
    std::shared_ptr<logger> log;
    if (async_mode) {
        init_thread_pool(8192, 1);
        log = std::make_shared<async_logger>("onep", sinks.begin(), sinks.end(),
            thread_pool(), async_overflow_policy::block);
    } else {
        log = std::make_shared<logger>("onep", sinks.begin(), sinks.end());
    }
    
    log->set_level(level::trace);
    set_default_logger(log);
    state.initialized = true;
}

#ifdef SPDLOG_ENABLE_MULTIPROCESS

/**
 * @brief 消费者配置结构体
 * 
 * 配置项互斥关系：
 * - shm_offset: 仅当 create_shm=false 时有效
 * - log_file: 设置后忽略 log_dir 和 log_name
 * - max_file_size, max_files: 仅当 enable_rotating=true 时有效
 * - block_timeout: 仅当 overflow_policy=Block 时有效
 */
struct ConsumerConfig {
    // ========== 共享内存配置 ==========
    std::string shm_name = "/spdlog_default_shm";   // 共享内存名称（默认值）
    size_t shm_size = 4 * 1024 * 1024;              // 共享内存大小（默认4MB）
    bool create_shm = true;                         // true=创建新的共享内存，false=连接已存在的
    size_t shm_offset = 0;                          // 共享内存偏移量（⚠️ 仅当 create_shm=false 时有效）
    
    // ========== 日志输出配置 ==========
    std::string log_dir = "logs/";                  // 日志目录（默认 "logs/"）
    std::string log_name = "app";                   // 日志名称（用于生成文件名：YYYYMMDD_HHMMSS_name.log）
    std::string log_file;                           // 完整日志文件路径（⚠️ 设置后忽略 log_dir 和 log_name）
    
    // ========== 日志轮转配置 ==========
    bool enable_rotating = true;                    // 是否启用日志轮转
    size_t max_file_size = 10 * 1024 * 1024;        // 单个日志文件最大大小（⚠️ 仅当 enable_rotating=true 时有效）
    size_t max_files = 5;                           // 最大日志文件数量（⚠️ 仅当 enable_rotating=true 时有效）
    
    // ========== 缓冲区配置 ==========
    size_t slot_size = 4096;                        // 槽位大小（字节）
    size_t slot_count = 0;                          // 槽位数量（0=自动计算）
    
    // ========== 溢出策略配置 ==========
    OverflowPolicy overflow_policy = OverflowPolicy::Block;  // 溢出策略：Block=阻塞等待，Drop=丢弃
    std::chrono::milliseconds block_timeout{5000};           // 阻塞超时时间（⚠️ 仅当 overflow_policy=Block 时有效）
    
    // ========== 消费者轮询配置 ==========
    std::chrono::milliseconds poll_interval{10};    // 轮询间隔（每次轮询之间的等待时间）
    std::chrono::milliseconds poll_duration{1000};  // 轮询时间（默认1秒后进入等待通知模式）
    
    // ========== 模式配置 ==========
    bool async_mode = false;                        // 是否使用异步模式
    bool enable_onep_format = false;                // 是否启用OnePet格式（默认false，使用标准格式）
    
    // ========== 通知模式配置 ==========
    NotifyMode notify_mode = NotifyMode::UDS;       // 通知模式（默认 UDS）
    std::string uds_path;                           // UDS 路径（空则自动生成）
    int eventfd = -1;                               // eventfd 文件描述符（可选，仅 EventFD 模式）
    
    // 便捷构造函数
    ConsumerConfig() = default;
    explicit ConsumerConfig(const std::string& name) : shm_name(name) {}
    ConsumerConfig(const std::string& name, const std::string& file) 
        : shm_name(name), log_file(file) {}
};

/**
 * @brief 生产者配置结构体
 * 
 * 配置项互斥关系：
 * - shm_handle 和 shm_name: 二选一，shm_handle 用于 fork 场景，shm_name 用于独立进程场景
 * - shm_size: 仅当使用 shm_name（独立进程场景）时有效
 * - block_timeout: 仅当 overflow_policy=Block 时有效
 */
struct ProducerConfig {
    // ========== 共享内存配置（二选一）==========
    SharedMemoryHandle shm_handle;                  // fork场景：使用父进程句柄（⚠️ 与 shm_name 互斥）
    std::string shm_name = "/spdlog_default_shm";   // 独立进程场景：共享内存名称（⚠️ 与 shm_handle 互斥）
    size_t shm_size = 4 * 1024 * 1024;              // 共享内存大小（⚠️ 仅独立进程场景有效）
    size_t shm_offset = 0;                          // 共享内存偏移量（使用共享内存的某一块区域）
    
    // ========== 缓冲区配置 ==========
    size_t slot_size = 4096;                        // 槽位大小（字节）
    
    // ========== 溢出策略配置 ==========
    OverflowPolicy overflow_policy = OverflowPolicy::Block;  // 溢出策略：Block=阻塞等待，Drop=丢弃
    std::chrono::milliseconds block_timeout{5000};           // 阻塞超时时间（⚠️ 仅当 overflow_policy=Block 时有效）
    
    // ========== 模式配置 ==========
    bool async_mode = false;                        // 是否使用异步模式
    bool enable_onep_format = false;                // 是否启用OnePet格式（默认false，使用标准格式）
    
    // ========== 通知模式配置 ==========
    NotifyMode notify_mode = NotifyMode::UDS;       // 通知模式（默认 UDS）
    std::string uds_path;                           // UDS 路径（空则自动生成）
    int eventfd = -1;                               // eventfd 文件描述符（可选，仅 EventFD 模式）
    
    // 便捷构造函数
    ProducerConfig() = default;
    
    // fork场景：使用父进程句柄
    explicit ProducerConfig(const SharedMemoryHandle& handle) : shm_handle(handle) {}
    
    // 独立进程场景：通过名称连接
    explicit ProducerConfig(const std::string& name, size_t size = 4 * 1024 * 1024) 
        : shm_name(name), shm_size(size) {}
};

/**
 * @brief 启用消费者模式
 * 
 * 创建共享内存、初始化消费者sink并自动启动消费者线程。
 * 
 * @param config 消费者配置（可选，使用默认配置）
 * @return 消费者sink指针（已自动启动）
 * 
 * 使用示例：
 * @code
 * // 最简单用法（使用所有默认配置）
 * spdlog::EnableConsumer();
 * 
 * // 指定共享内存名称
 * spdlog::EnableConsumer({"/my_shm"});
 * 
 * // 完整配置
 * spdlog::ConsumerConfig cfg;
 * cfg.shm_name = "/my_shm";
 * cfg.log_name = "myapp";                          // 日志文件名：YYYYMMDD_HHMMSS_myapp.log
 * cfg.shm_size = 8 * 1024 * 1024;                  // 8MB
 * cfg.max_file_size = 20 * 1024 * 1024;            // 单文件最大20MB
 * cfg.max_files = 10;                              // 最多保留10个文件
 * spdlog::EnableConsumer(cfg);
 * @endcode
 */
inline std::shared_ptr<multiprocess::SharedMemoryConsumerSink> EnableConsumer(
    const ConsumerConfig& config = ConsumerConfig()) {
    
    auto& state = detail::OnepState::instance();
    state.async_mode = config.async_mode;
    
    // 根据create_shm决定是否清理和销毁
    bool destroy_on_exit = config.create_shm;
    
    // shm_offset 仅在 create_shm=false 时有效
    size_t effective_offset = config.create_shm ? 0 : config.shm_offset;
    
    if (config.create_shm) {
        // 创建新的共享内存，先清理旧的
        shm_unlink(config.shm_name.c_str());
        
        auto result = SharedMemoryManager::create(config.shm_size, config.shm_name);
        if (result.is_error()) {
            return nullptr;
        }
        state.shm_handle = result.value();
    } else {
        // 连接到已存在的共享内存
        int fd = shm_open(config.shm_name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            return nullptr;
        }
        state.shm_handle.name = config.shm_name;
        state.shm_handle.size = config.shm_size;
        state.shm_handle.fd = fd;
    }
    
    // 确定日志文件路径
    std::string log_filename;
    if (!config.log_file.empty()) {
        // 用户指定了完整路径
        log_filename = config.log_file;
    } else {
        // 自动生成：logs/YYYYMMDD_HHMMSS_name.log
        log_filename = detail::GenerateLogFilename(config.log_dir, config.log_name);
    }
    
    // 创建输出sinks
    std::vector<sink_ptr> output_sinks;
    
    if (config.enable_onep_format) {
        // OnePet格式
        output_sinks.push_back(detail::CreateConsumerConsoleSink());
        if (config.enable_rotating) {
            output_sinks.push_back(detail::CreateConsumerRotatingFileSink(
                log_filename, config.max_file_size, config.max_files));
        } else {
            output_sinks.push_back(detail::CreateConsumerFileSink(log_filename));
        }
    } else {
        // 标准格式
        output_sinks.push_back(detail::CreateStandardConsoleSink());
        if (config.enable_rotating) {
            output_sinks.push_back(detail::CreateStandardRotatingFileSink(
                log_filename, config.max_file_size, config.max_files));
        } else {
            output_sinks.push_back(detail::CreateStandardFileSink(log_filename));
        }
    }
    
    // 创建消费者配置
    multiprocess::ConsumerConfig consumer_cfg;
    consumer_cfg.poll_interval = config.poll_interval;
    consumer_cfg.poll_duration = config.poll_duration;
    consumer_cfg.destroy_on_exit = destroy_on_exit;
    consumer_cfg.enable_onep_format = config.enable_onep_format;
    consumer_cfg.slot_size = config.slot_size;  // 传递槽位大小
#ifdef NDEBUG
    consumer_cfg.debug_format = false;  // Release 模式：不显示 PID 和 ThreadID
#else
    consumer_cfg.debug_format = true;   // Debug 模式：显示 PID 和 ThreadID
#endif
    
    // 传递通知模式配置
    consumer_cfg.notify_mode = config.notify_mode;
    consumer_cfg.uds_path = config.uds_path;
    consumer_cfg.eventfd = config.eventfd;
    
    try {
        state.consumer_sink = std::make_shared<multiprocess::SharedMemoryConsumerSink>(
            state.shm_handle, output_sinks, consumer_cfg, effective_offset);
    } catch (...) {
        if (!config.create_shm) {
            close(state.shm_handle.fd);
        }
        return nullptr;
    }
    
    // 创建生产者logger供主进程使用
    multiprocess::ProducerConfig prod_cfg;
    prod_cfg.slot_size = config.slot_size;
    prod_cfg.overflow_policy = config.overflow_policy;
    prod_cfg.block_timeout = config.block_timeout;
    
    auto producer_sink = std::make_shared<multiprocess::shared_memory_producer_sink_mt>(
        state.shm_handle, prod_cfg, effective_offset);
    
    // 创建logger并设置为default logger
    std::shared_ptr<logger> log;
    if (config.async_mode) {
        init_thread_pool(8192, 1);
        log = std::make_shared<async_logger>("onep", producer_sink,
            thread_pool(), async_overflow_policy::block);
    } else {
        log = std::make_shared<logger>("onep", producer_sink);
    }
    
    log->set_level(level::trace);
    set_default_logger(log);
    state.initialized = true;
    state.creator_pid = getpid();  // 记录创建者进程ID
    
    // 自动启动消费者线程
    state.consumer_sink->start();
    
    return state.consumer_sink;
}

/**
 * @brief 启用生产者模式
 * 
 * @param config 生产者配置（可选，使用默认配置）
 * @return 成功返回true，失败返回false
 * 
 * @note 
 * - 自动检测是否是 fork 的子进程并清理继承的状态
 * - fork 子进程默认使用同步模式（更安全、更快）
 * - 如果需要在子进程中使用异步模式，设置 async_mode=true，
 *   会创建新的线程池（而不是使用继承的无效线程池）
 * 
 * @warning 子进程退出时建议使用 _exit() 或 _Exit()
 */
inline bool EnableProducer(const ProducerConfig& config = ProducerConfig()) {
    auto& state = detail::OnepState::instance();
    
    // 自动检测是否是 fork 的子进程
    bool is_forked = state.is_forked_child();
    
    if (is_forked) {
        // fork 子进程：清空继承的状态，但不调用析构函数
        state.consumer_sink.reset();
        
        // 重要：先清空 default logger，避免继承的 async_logger 持有无效线程池引用
        // 使用 drop_all() 会触发析构，可能导致问题，所以直接设置一个临时的同步 logger
        auto null_sink = std::make_shared<sinks::null_sink_mt>();
        auto temp_logger = std::make_shared<logger>("temp", null_sink);
        set_default_logger(temp_logger);
        
        // 重置线程池指针（不 join，因为线程在父进程中）
        details::registry::instance().set_tp(nullptr);
    }
    
    // 清空模块名和进程名
    {
        std::lock_guard<std::mutex> lock(state.module_mutex);
        state.module_names.clear();
    }
    state.process_name = "";
    state.async_mode = config.async_mode;
    
    // 判断使用哪种方式连接共享内存
    if (config.shm_handle.fd >= 0) {
        state.shm_handle = config.shm_handle;
    } else if (!config.shm_name.empty()) {
        int fd = shm_open(config.shm_name.c_str(), O_RDWR, 0666);
        if (fd == -1) return false;
        state.shm_handle.name = config.shm_name;
        state.shm_handle.size = config.shm_size;
        state.shm_handle.fd = fd;
    } else {
        return false;
    }
    
    multiprocess::ProducerConfig prod_cfg;
    prod_cfg.slot_size = config.slot_size;
    prod_cfg.overflow_policy = config.overflow_policy;
    prod_cfg.block_timeout = config.block_timeout;
    
    // 传递通知模式配置
    // 注意：生产者实际上从共享内存元数据读取通知模式
    // 这里的配置主要用于 EventFD 模式下传递 eventfd 文件描述符
    prod_cfg.notify_mode = config.notify_mode;
    prod_cfg.uds_path = config.uds_path;
    prod_cfg.eventfd = config.eventfd;
    
    auto producer_sink = std::make_shared<multiprocess::shared_memory_producer_sink_mt>(
        state.shm_handle, prod_cfg, config.shm_offset);
    
    if (!producer_sink->is_shared_memory_available()) return false;
    
    // 创建 logger
    std::shared_ptr<logger> log;
    if (config.async_mode) {
        // 异步模式：创建新的线程池（不使用继承的无效线程池）
        init_thread_pool(8192, 1);
        log = std::make_shared<async_logger>("onep", producer_sink,
            thread_pool(), async_overflow_policy::block);
    } else {
        // 同步模式（推荐，更快更安全）
        log = std::make_shared<logger>("onep", producer_sink);
    }
    
    log->set_level(level::trace);
    set_default_logger(log);
    state.initialized = true;
    state.creator_pid = getpid();
    
    return true;
}

/**
 * @brief 获取共享内存句柄（供fork子进程使用）
 */
inline SharedMemoryHandle GetSharedMemoryHandle() {
    return detail::OnepState::instance().shm_handle;
}

/**
 * @brief 重置 spdlog 状态（fork 后子进程可选调用）
 * 
 * @details
 * 在 fork() 后，子进程会继承父进程的 spdlog 状态，包括：
 * - 异步线程池（在子进程中无效）
 * - 已注册的 logger
 * - 消费者 sink
 * 
 * @note 
 * - EnableProducer() 会自动处理 fork 后的状态清理，通常不需要手动调用此函数
 * - 此函数主要用于需要完全控制清理过程的高级场景
 * - 子进程退出时建议使用 _exit() 或 _Exit() 而不是 exit()
 * 
 * 使用示例：
 * @code
 * auto handle = spdlog::GetSharedMemoryHandle();
 * if (fork() == 0) {
 *     // 子进程 - EnableProducer 会自动清理状态
 *     spdlog::EnableProducer(spdlog::ProducerConfig(handle));
 *     spdlog::SetProcessName("Child");
 *     spdlog::info("Hello from child!");
 *     _exit(0);  // 使用 _exit 避免调用 atexit 处理器
 * }
 * @endcode
 */
inline void ResetForFork() {
    auto& state = detail::OnepState::instance();
    
    // 清空消费者 sink 引用（不调用 stop，因为消费者在父进程中运行）
    state.consumer_sink.reset();
    
    // 重置进程名和模块名
    state.process_name = "";
    {
        std::lock_guard<std::mutex> lock(state.module_mutex);
        state.module_names.clear();
    }
    
    state.initialized = false;
    state.async_mode = false;
    
    // 注意：我们不调用 drop_all() 或 set_tp(nullptr)
    // 因为这可能触发 async_logger 的析构函数，导致 "libc++abi: terminating" 错误
    // 旧的 logger 会泄漏，但在 fork 场景下这是安全的做法
}

#endif // SPDLOG_ENABLE_MULTIPROCESS

/**
 * @brief 关闭日志系统
 */
inline void Shutdown() {
    auto& state = detail::OnepState::instance();
    
#ifdef SPDLOG_ENABLE_MULTIPROCESS
    if (state.consumer_sink) {
        state.consumer_sink->stop();
        state.consumer_sink->flush_output_sinks();
        state.consumer_sink.reset();
    }
#endif
    
    spdlog::shutdown();
    state.initialized = false;
}

} // namespace spdlog
