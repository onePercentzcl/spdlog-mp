// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

/**
 * @file example_mp4.cpp
 * @brief 多进程多线程极限测试 - 测试非阻塞模式下的日志丢失
 * 
 * 测试配置：
 * - 共享内存大小：512KB
 * - 槽位大小：512字节
 * - 槽位数量：1024个
 * - 进程数：10个
 * - 每进程线程数：10个
 * - 溢出策略：Drop（非阻塞，丢弃）
 * 
 * 测试目标：
 * - 通过控制每个线程的日志产生速度，找到不丢失日志的极限速率
 * - 测试不同速率下的丢失率（精度100ns）
 * - 通过统计日志文件行数验证实际写入数量
 */

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <iomanip>
#include <cmath>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std::chrono;

// ============================================================================
// 测试配置
// ============================================================================
struct TestConfig {
    size_t shm_size = 512 * 1024;           // 共享内存大小 512KB
    size_t slot_size = 512;                  // 槽位大小 512字节
    size_t process_count = 10;               // 进程数量
    size_t threads_per_process = 10;         // 每进程线程数
    size_t messages_per_thread = 1000;       // 每线程消息数量
    size_t message_length = 100;             // 消息长度（字节）
    int delay_ns = 0;                        // 每条消息后的延迟（纳秒）
    bool enable_console = false;             // 是否启用控制台输出
    std::string log_file;                    // 日志文件路径
};

// ============================================================================
// 共享计数器（用于多进程统计）
// ============================================================================
struct SharedCounters {
    std::atomic<size_t> produced_count{0};   // 尝试生产的消息数
    std::atomic<int> ready_processes{0};     // 准备就绪的进程数
    std::atomic<bool> start_signal{false};   // 开始信号
};

// ============================================================================
// 测试结果
// ============================================================================
struct TestResult {
    size_t total_produced = 0;
    size_t total_written = 0;      // 实际写入日志文件的行数
    size_t total_dropped = 0;
    double duration_ms = 0;
    double throughput = 0;
    double drop_rate = 0;
    
    void print(int delay_ns) const {
        std::cout << std::setw(10) << delay_ns
                  << std::setw(12) << total_produced
                  << std::setw(12) << total_written
                  << std::setw(12) << total_dropped
                  << std::setw(10) << std::fixed << std::setprecision(2) << drop_rate << "%"
                  << std::setw(15) << std::fixed << std::setprecision(0) << throughput
                  << std::setw(12) << std::fixed << std::setprecision(2) << duration_ms << "\n";
    }
};

// ============================================================================
// 生成测试消息
// ============================================================================
std::string generate_message(size_t length, int process_id, int thread_id, size_t msg_id) {
    std::string msg = "P" + std::to_string(process_id) + 
                      "T" + std::to_string(thread_id) + 
                      "-" + std::to_string(msg_id) + "-";
    if (msg.length() < length) {
        msg.append(length - msg.length(), 'X');
    }
    return msg.substr(0, length);
}

// ============================================================================
// 统计日志文件行数
// ============================================================================
size_t count_log_lines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return 0;
    }
    
    size_t count = 0;
    std::string line;
    while (std::getline(file, line)) {
        // 只统计包含测试消息的行（以 P 开头的消息）
        if (line.find("] P") != std::string::npos) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// 生产者线程函数
// ============================================================================
void producer_thread(int process_id, int thread_id, const TestConfig& cfg, 
                     SharedCounters* counters) {
    spdlog::SetModuleName("P" + std::to_string(process_id) + "T" + std::to_string(thread_id));
    
    // 等待开始信号
    while (!counters->start_signal.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    
    for (size_t i = 0; i < cfg.messages_per_thread; ++i) {
        std::string msg = generate_message(cfg.message_length, process_id, thread_id, i);
        
        counters->produced_count.fetch_add(1, std::memory_order_relaxed);
        
        // 使用 info 级别记录日志
        spdlog::info("{}", msg);
        
        // 延迟控制速率（纳秒级）
        if (cfg.delay_ns > 0) {
            std::this_thread::sleep_for(nanoseconds(cfg.delay_ns));
        }
    }
}

// ============================================================================
// 运行单次测试
// ============================================================================
TestResult run_test(const TestConfig& cfg) {
    TestResult result;
    
    const char* shm_name = "/mp4_test";
    const char* counter_shm_name = "/mp4_counter";
    
    // 清理旧的共享内存
    shm_unlink(shm_name);
    shm_unlink(counter_shm_name);
    
    // 生成唯一的日志文件名
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream log_filename;
    log_filename << "/tmp/mp4_test_" << cfg.delay_ns << "ns_"
                 << std::setfill('0') << std::setw(2) << tm_now.tm_hour
                 << std::setw(2) << tm_now.tm_min
                 << std::setw(2) << tm_now.tm_sec
                 << ".log";
    std::string log_file = log_filename.str();
    
    // 创建共享计数器
    int counter_fd = shm_open(counter_shm_name, O_CREAT | O_RDWR, 0666);
    if (counter_fd == -1) {
        std::cerr << "创建计数器共享内存失败\n";
        return result;
    }
    ftruncate(counter_fd, sizeof(SharedCounters));
    auto* counters = static_cast<SharedCounters*>(
        mmap(nullptr, sizeof(SharedCounters), PROT_READ | PROT_WRITE, MAP_SHARED, counter_fd, 0));
    new (counters) SharedCounters();
    
    // 配置消费者 - 使用 Drop 策略
    spdlog::ConsumerConfig consumer_cfg;
    consumer_cfg.shm_name = shm_name;
    consumer_cfg.shm_size = cfg.shm_size;
    consumer_cfg.slot_size = cfg.slot_size;
    consumer_cfg.create_shm = true;
    consumer_cfg.async_mode = false;
    consumer_cfg.log_file = log_file;           // 使用指定的日志文件
    consumer_cfg.enable_rotating = false;
    consumer_cfg.overflow_policy = spdlog::OverflowPolicy::Drop;  // 非阻塞，丢弃
    consumer_cfg.enable_console = cfg.enable_console;
    consumer_cfg.poll_interval = std::chrono::milliseconds(1);  // 1ms 轮询间隔
    
    auto consumer = spdlog::EnableConsumer(consumer_cfg);
    if (!consumer) {
        std::cerr << "启用消费者失败\n";
        munmap(counters, sizeof(SharedCounters));
        close(counter_fd);
        shm_unlink(counter_shm_name);
        return result;
    }
    
    spdlog::SetProcessName("Main");
    
    auto handle = spdlog::GetSharedMemoryHandle();
    
    // Fork 子进程
    std::vector<pid_t> children;
    for (size_t p = 0; p < cfg.process_count; ++p) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            spdlog::ProducerConfig prod_cfg;
            prod_cfg.shm_handle = handle;
            prod_cfg.slot_size = cfg.slot_size;
            prod_cfg.overflow_policy = spdlog::OverflowPolicy::Drop;
            prod_cfg.async_mode = false;
            
            if (!spdlog::EnableProducer(prod_cfg)) {
                _exit(1);
            }
            
            spdlog::SetProcessName("P" + std::to_string(p));
            
            // 标记进程准备就绪
            counters->ready_processes.fetch_add(1, std::memory_order_release);
            
            // 创建线程
            std::vector<std::thread> threads;
            for (size_t t = 0; t < cfg.threads_per_process; ++t) {
                threads.emplace_back(producer_thread, p, t, std::ref(cfg), counters);
            }
            
            for (auto& th : threads) {
                th.join();
            }
            
            spdlog::default_logger()->flush();
            _exit(0);
        } else if (pid > 0) {
            children.push_back(pid);
        }
    }
    
    // 等待所有子进程准备就绪
    while (counters->ready_processes.load(std::memory_order_acquire) < (int)cfg.process_count) {
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    // 发送开始信号并开始计时
    auto start = high_resolution_clock::now();
    counters->start_signal.store(true, std::memory_order_release);
    
    // 等待所有子进程完成
    for (pid_t pid : children) {
        int status;
        waitpid(pid, &status, 0);
    }
    
    auto end = high_resolution_clock::now();
    
    // 等待消费者处理完剩余消息
    std::this_thread::sleep_for(milliseconds(1000));
    
    // 关闭日志系统，确保所有消息都写入文件
    spdlog::Shutdown();
    
    // 统计结果
    result.total_produced = counters->produced_count.load();
    result.duration_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
    
    // 统计日志文件行数
    result.total_written = count_log_lines(log_file);
    result.total_dropped = result.total_produced > result.total_written ? 
                           result.total_produced - result.total_written : 0;
    result.drop_rate = result.total_produced > 0 ? 
        (double)result.total_dropped / result.total_produced * 100.0 : 0;
    result.throughput = result.total_written / (result.duration_ms / 1000.0);
    
    // 清理
    munmap(counters, sizeof(SharedCounters));
    close(counter_fd);
    shm_unlink(shm_name);
    shm_unlink(counter_shm_name);
    
    // 删除测试日志文件
    unlink(log_file.c_str());
    
    return result;
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "   spdlog-mp 极限测试 (example_mp4)\n";
    std::cout << "========================================\n";
    std::cout << "测试配置:\n";
    std::cout << "  共享内存大小: 4MB\n";
    std::cout << "  槽位大小: 512字节\n";
    std::cout << "  槽位数量: 8192个\n";
    std::cout << "  进程数: 10\n";
    std::cout << "  每进程线程数: 10\n";
    std::cout << "  总生产者: 100个\n";
    std::cout << "  溢出策略: Drop（非阻塞）\n";
    std::cout << "========================================\n\n";
    
    TestConfig cfg;
    cfg.shm_size = 4 * 1024 * 1024;       // 4MB（增大共享内存）
    cfg.slot_size = 512;                   // 512字节
    cfg.process_count = 10;
    cfg.threads_per_process = 10;
    cfg.messages_per_thread = 10000;
    cfg.message_length = 100;
    cfg.enable_console = false;
    
    // 检查命令行参数
    bool quick_test = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--quick" || arg == "-q") {
            quick_test = true;
            cfg.messages_per_thread = 1000;
        } else if (arg == "--console" || arg == "-c") {
            cfg.enable_console = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "用法: " << argv[0] << " [选项]\n";
            std::cout << "选项:\n";
            std::cout << "  --quick, -q    快速测试（减少消息数量）\n";
            std::cout << "  --console, -c  启用控制台输出\n";
            std::cout << "  --help, -h     显示帮助信息\n";
            return 0;
        }
    }
    
    size_t total_messages = cfg.process_count * cfg.threads_per_process * cfg.messages_per_thread;
    std::cout << "每线程消息数: " << cfg.messages_per_thread << "\n";
    std::cout << "总消息数: " << total_messages << "\n\n";
    
    // 测试不同延迟下的丢失率
    std::cout << "========== 不同延迟下的丢失率测试 ==========\n";
    std::cout << std::setw(10) << "延迟(ns)"
              << std::setw(12) << "尝试写入"
              << std::setw(12) << "实际写入"
              << std::setw(12) << "丢弃数"
              << std::setw(10) << "丢失率"
              << std::setw(15) << "吞吐量(msg/s)"
              << std::setw(12) << "耗时(ms)" << "\n";
    std::cout << std::string(81, '-') << "\n";
    
    // 测试不同的延迟值（纳秒）
    // 从微秒到毫秒级别，寻找零丢失阈值
    std::vector<int> delays;
    if (quick_test) {
        // 快速测试：重点测试 200μs - 500μs 区间
        delays = {0, 10000, 50000, 100000, 150000, 200000, 250000, 300000, 350000, 400000, 450000, 500000, 1000000};
    } else {
        // 完整测试：更细粒度，从 1μs 到 50ms
        delays = {0, 1000, 5000, 10000, 20000, 50000, 100000, 150000, 200000, 250000, 300000, 350000, 400000, 450000, 500000,
                  600000, 700000, 800000, 900000, 1000000, 1500000, 2000000, 5000000, 10000000};
    }
    
    int zero_drop_delay = -1;
    for (int delay : delays) {
        cfg.delay_ns = delay;
        auto result = run_test(cfg);
        
        // 格式化延迟显示
        std::string delay_str;
        if (delay >= 1000000) {
            delay_str = std::to_string(delay / 1000000) + "ms";
        } else if (delay >= 1000) {
            delay_str = std::to_string(delay / 1000) + "μs";
        } else {
            delay_str = std::to_string(delay) + "ns";
        }
        std::cout << std::setw(10) << delay_str;
        std::cout << std::setw(12) << result.total_produced
                  << std::setw(12) << result.total_written
                  << std::setw(12) << result.total_dropped
                  << std::setw(10) << std::fixed << std::setprecision(2) << result.drop_rate << "%"
                  << std::setw(15) << std::fixed << std::setprecision(0) << result.throughput
                  << std::setw(12) << std::fixed << std::setprecision(2) << result.duration_ms << "\n";
        
        // 记录第一个零丢失的延迟
        if (result.drop_rate == 0 && zero_drop_delay < 0) {
            zero_drop_delay = delay;
        }
        
        // 测试间隔
        std::this_thread::sleep_for(milliseconds(500));
    }
    
    std::cout << "\n========================================\n";
    std::cout << "   测试完成\n";
    std::cout << "========================================\n";
    
    if (zero_drop_delay >= 0) {
        std::cout << "\n零丢失的最小延迟阈值: " << zero_drop_delay << " ns";
        if (zero_drop_delay >= 1000) {
            std::cout << " (" << zero_drop_delay / 1000.0 << " μs)";
        }
        std::cout << "\n";
    } else {
        std::cout << "\n警告: 所有测试都有日志丢失\n";
    }
    
    return 0;
}
