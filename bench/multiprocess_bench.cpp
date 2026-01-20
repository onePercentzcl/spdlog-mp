// 多进程共享内存日志性能基准测试
// 编译：需要启用 SPDLOG_ENABLE_MULTIPROCESS

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/async.h>

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

static const char* SHM_NAME = "/spdlog_bench";
static const size_t SHM_SIZE = 16 * 1024 * 1024;  // 16MB
static const int WARMUP_COUNT = 100;  // 减少预热次数，避免过多输出

// ============================================================
// 自定义消费者Sink（只输出到null，不输出到控制台）
// ============================================================

// 创建只输出到null的消费者（用于基准测试，避免控制台输出）
inline std::shared_ptr<spdlog::multiprocess::SharedMemoryConsumerSink> CreateBenchConsumer(
    const spdlog::SharedMemoryHandle& handle,
    const spdlog::multiprocess::ConsumerConfig& cfg,
    size_t offset = 0) {
    
    // 只使用null sink，不输出到控制台，避免大量日志输出
    std::vector<spdlog::sink_ptr> output_sinks;
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    output_sinks.push_back(null_sink);
    
    return std::make_shared<spdlog::multiprocess::SharedMemoryConsumerSink>(
        handle, output_sinks, cfg, offset);
}

// 计算百分位数
template<typename T>
T percentile(std::vector<T>& data, double p) {
    if (data.empty()) return T{};
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
}

// 格式化数字（添加千位分隔符）
std::string format_number(double num) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << num;
    std::string s = oss.str();
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

// ============================================================
// 原版 spdlog 基准测试（用于对比）
// ============================================================

void bench_original_spdlog_sync(int num_messages) {
    std::cout << "\n=== 原版 spdlog 同步模式 ===" << std::endl;
    
    auto logger = spdlog::basic_logger_mt("bench_sync", "/dev/null", true);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        logger->info("Warmup message {}", i);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_messages; ++i) {
        logger->info("Benchmark message number {} with some additional content", i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = num_messages / seconds;
    
    std::cout << "消息数量: " << format_number(num_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    spdlog::drop("bench_sync");
}

void bench_original_spdlog_async(int num_messages) {
    std::cout << "\n=== 原版 spdlog 异步模式 ===" << std::endl;
    
    spdlog::init_thread_pool(8192, 1);
    auto logger = spdlog::basic_logger_mt<spdlog::async_factory>("bench_async", "/dev/null", true);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        logger->info("Warmup message {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_messages; ++i) {
        logger->info("Benchmark message number {} with some additional content", i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = num_messages / seconds;
    
    std::cout << "消息数量: " << format_number(num_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    spdlog::drop("bench_async");
    spdlog::shutdown();
}

void bench_original_spdlog_multithread(int num_threads, int messages_per_thread) {
    std::cout << "\n=== 原版 spdlog 多线程同步模式 ===" << std::endl;
    std::cout << "线程数: " << num_threads << std::endl;
    std::cout << "每线程消息数: " << format_number(messages_per_thread) << std::endl;
    
    auto logger = spdlog::basic_logger_mt("bench_mt", "/dev/null", true);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        logger->info("Warmup {}", i);
    }
    
    std::atomic<int> ready_count{0};
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            ready_count++;
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            
            for (int i = 0; i < messages_per_thread; ++i) {
                logger->info("Thread {} message {}", t, i);
            }
        });
    }
    
    while (ready_count.load() < num_threads) {
        std::this_thread::yield();
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    start_flag.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    int total_messages = num_threads * messages_per_thread;
    double seconds = duration.count() / 1000000.0;
    double throughput = total_messages / seconds;
    
    std::cout << "总消息数: " << format_number(total_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    spdlog::drop("bench_mt");
}


// ============================================================
// spdlog-mp 多进程共享内存基准测试
// ============================================================

void bench_mp_single_process(int num_messages, bool async_mode, bool onep_format) {
    std::cout << "\n=== spdlog-mp 单进程写入 ===" << std::endl;
    std::cout << "异步模式: " << (async_mode ? "是" : "否") << std::endl;
    std::cout << "OnePet格式: " << (onep_format ? "是" : "否") << std::endl;
    
    shm_unlink(SHM_NAME);
    
    // 创建共享内存
    auto result = spdlog::SharedMemoryManager::create(SHM_SIZE, SHM_NAME);
    if (result.is_error()) {
        std::cerr << "创建共享内存失败" << std::endl;
        return;
    }
    auto shm_handle = result.value();
    
    // 创建消费者配置
    spdlog::multiprocess::ConsumerConfig consumer_cfg;
    consumer_cfg.poll_interval = std::chrono::milliseconds(1);
    consumer_cfg.destroy_on_exit = true;
    consumer_cfg.enable_onep_format = onep_format;
    
    // 创建消费者（只输出到null sink）
    auto consumer = CreateBenchConsumer(shm_handle, consumer_cfg);
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return;
    }
    consumer->start();
    
    // 创建生产者
    spdlog::multiprocess::ProducerConfig prod_cfg;
    prod_cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    
    auto producer_sink = std::make_shared<spdlog::multiprocess::shared_memory_producer_sink_mt>(
        shm_handle, prod_cfg, 0);
    
    std::shared_ptr<spdlog::logger> log;
    if (async_mode) {
        spdlog::init_thread_pool(8192, 1);
        log = std::make_shared<spdlog::async_logger>("bench", producer_sink,
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    } else {
        log = std::make_shared<spdlog::logger>("bench", producer_sink);
    }
    log->set_level(spdlog::level::trace);
    spdlog::set_default_logger(log);
    
    if (onep_format) {
        spdlog::SetProcessName("Bench");
        spdlog::SetModuleName("Test");
    }
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        spdlog::info("Warmup message {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_messages; ++i) {
        spdlog::info("Benchmark message number {} with some additional content", i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = num_messages / seconds;
    
    std::cout << "消息数量: " << format_number(num_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    consumer->stop();
    spdlog::drop_all();
    spdlog::shutdown();
}

void bench_mp_latency(int num_samples, bool async_mode, bool onep_format) {
    std::cout << "\n=== spdlog-mp 写入延迟测试 ===" << std::endl;
    std::cout << "异步模式: " << (async_mode ? "是" : "否") << std::endl;
    std::cout << "OnePet格式: " << (onep_format ? "是" : "否") << std::endl;
    
    shm_unlink(SHM_NAME);
    
    // 创建共享内存
    auto result = spdlog::SharedMemoryManager::create(SHM_SIZE, SHM_NAME);
    if (result.is_error()) {
        std::cerr << "创建共享内存失败" << std::endl;
        return;
    }
    auto shm_handle = result.value();
    
    // 创建消费者配置
    spdlog::multiprocess::ConsumerConfig consumer_cfg;
    consumer_cfg.poll_interval = std::chrono::milliseconds(1);
    consumer_cfg.destroy_on_exit = true;
    consumer_cfg.enable_onep_format = onep_format;
    
    // 创建消费者（只输出到null sink）
    auto consumer = CreateBenchConsumer(shm_handle, consumer_cfg);
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return;
    }
    consumer->start();
    
    // 创建生产者
    spdlog::multiprocess::ProducerConfig prod_cfg;
    prod_cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    
    auto producer_sink = std::make_shared<spdlog::multiprocess::shared_memory_producer_sink_mt>(
        shm_handle, prod_cfg, 0);
    
    std::shared_ptr<spdlog::logger> log;
    if (async_mode) {
        spdlog::init_thread_pool(8192, 1);
        log = std::make_shared<spdlog::async_logger>("bench", producer_sink,
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    } else {
        log = std::make_shared<spdlog::logger>("bench", producer_sink);
    }
    log->set_level(spdlog::level::trace);
    spdlog::set_default_logger(log);
    
    if (onep_format) {
        spdlog::SetProcessName("Bench");
        spdlog::SetModuleName("Test");
    }
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        spdlog::info("Warmup {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<int64_t> latencies;
    latencies.reserve(num_samples);
    
    for (int i = 0; i < num_samples; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        spdlog::info("Latency test message {}", i);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        latencies.push_back(latency);
    }
    
    int64_t avg = std::accumulate(latencies.begin(), latencies.end(), 0LL) / latencies.size();
    int64_t p50 = percentile(latencies, 0.50);
    int64_t p95 = percentile(latencies, 0.95);
    int64_t p99 = percentile(latencies, 0.99);
    int64_t p999 = percentile(latencies, 0.999);
    
    std::cout << "样本数: " << format_number(num_samples) << std::endl;
    std::cout << "平均延迟: " << avg << " ns (" << std::fixed << std::setprecision(2) << avg/1000.0 << " μs)" << std::endl;
    std::cout << "P50: " << p50 << " ns (" << p50/1000.0 << " μs)" << std::endl;
    std::cout << "P95: " << p95 << " ns (" << p95/1000.0 << " μs)" << std::endl;
    std::cout << "P99: " << p99 << " ns (" << p99/1000.0 << " μs)" << std::endl;
    std::cout << "P99.9: " << p999 << " ns (" << p999/1000.0 << " μs)" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    consumer->stop();
    spdlog::drop_all();
    spdlog::shutdown();
}

void bench_mp_multithread(int num_threads, int messages_per_thread, bool async_mode, bool onep_format) {
    std::cout << "\n=== spdlog-mp 多线程写入 ===" << std::endl;
    std::cout << "线程数: " << num_threads << std::endl;
    std::cout << "每线程消息数: " << format_number(messages_per_thread) << std::endl;
    std::cout << "异步模式: " << (async_mode ? "是" : "否") << std::endl;
    std::cout << "OnePet格式: " << (onep_format ? "是" : "否") << std::endl;
    
    shm_unlink(SHM_NAME);
    
    // 创建共享内存
    auto result = spdlog::SharedMemoryManager::create(SHM_SIZE, SHM_NAME);
    if (result.is_error()) {
        std::cerr << "创建共享内存失败" << std::endl;
        return;
    }
    auto shm_handle = result.value();
    
    // 创建消费者配置
    spdlog::multiprocess::ConsumerConfig consumer_cfg;
    consumer_cfg.poll_interval = std::chrono::milliseconds(1);
    consumer_cfg.destroy_on_exit = true;
    consumer_cfg.enable_onep_format = onep_format;
    
    // 创建消费者（只输出到null sink）
    auto consumer = CreateBenchConsumer(shm_handle, consumer_cfg);
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return;
    }
    consumer->start();
    
    // 创建生产者
    spdlog::multiprocess::ProducerConfig prod_cfg;
    prod_cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    
    auto producer_sink = std::make_shared<spdlog::multiprocess::shared_memory_producer_sink_mt>(
        shm_handle, prod_cfg, 0);
    
    std::shared_ptr<spdlog::logger> log;
    if (async_mode) {
        spdlog::init_thread_pool(8192, 1);
        log = std::make_shared<spdlog::async_logger>("bench", producer_sink,
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    } else {
        log = std::make_shared<spdlog::logger>("bench", producer_sink);
    }
    log->set_level(spdlog::level::trace);
    spdlog::set_default_logger(log);
    
    if (onep_format) {
        spdlog::SetProcessName("Bench");
        spdlog::SetModuleName("MT");
    }
    
    // 预热
    for (int i = 0; i < WARMUP_COUNT; ++i) {
        spdlog::info("Warmup {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::atomic<int> ready_count{0};
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t, onep_format]() {
            if (onep_format) {
                spdlog::SetModuleName("T" + std::to_string(t));
            }
            
            ready_count++;
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            
            for (int i = 0; i < messages_per_thread; ++i) {
                spdlog::info("Thread {} message {}", t, i);
            }
        });
    }
    
    while (ready_count.load() < num_threads) {
        std::this_thread::yield();
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    start_flag.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    int total_messages = num_threads * messages_per_thread;
    double seconds = duration.count() / 1000000.0;
    double throughput = total_messages / seconds;
    
    std::cout << "总消息数: " << format_number(total_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    consumer->stop();
    spdlog::drop_all();
    spdlog::shutdown();
}


void bench_mp_multiprocess(int num_processes, int messages_per_process, bool async_mode, bool onep_format) {
    std::cout << "\n=== spdlog-mp 多进程写入 ===" << std::endl;
    std::cout << "进程数: " << num_processes << std::endl;
    std::cout << "每进程消息数: " << format_number(messages_per_process) << std::endl;
    std::cout << "异步模式: " << (async_mode ? "是" : "否") << std::endl;
    std::cout << "OnePet格式: " << (onep_format ? "是" : "否") << std::endl;
    
    shm_unlink(SHM_NAME);
    
    // 创建共享内存
    auto result = spdlog::SharedMemoryManager::create(SHM_SIZE, SHM_NAME);
    if (result.is_error()) {
        std::cerr << "创建共享内存失败" << std::endl;
        return;
    }
    auto shm_handle = result.value();
    
    // 创建消费者配置
    spdlog::multiprocess::ConsumerConfig consumer_cfg;
    consumer_cfg.poll_interval = std::chrono::milliseconds(1);
    consumer_cfg.destroy_on_exit = true;
    consumer_cfg.enable_onep_format = onep_format;
    
    // 创建消费者（只输出到null sink）
    auto consumer = CreateBenchConsumer(shm_handle, consumer_cfg);
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return;
    }
    consumer->start();
    
    // 主进程也创建生产者
    spdlog::multiprocess::ProducerConfig prod_cfg;
    prod_cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    
    auto producer_sink = std::make_shared<spdlog::multiprocess::shared_memory_producer_sink_mt>(
        shm_handle, prod_cfg, 0);
    
    std::shared_ptr<spdlog::logger> log;
    if (async_mode) {
        spdlog::init_thread_pool(8192, 1);
        log = std::make_shared<spdlog::async_logger>("bench", producer_sink,
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    } else {
        log = std::make_shared<spdlog::logger>("bench", producer_sink);
    }
    log->set_level(spdlog::level::trace);
    spdlog::set_default_logger(log);
    
    if (onep_format) {
        spdlog::SetProcessName("Main");
        spdlog::SetModuleName("Bench");
    }
    
    // 预热
    for (int i = 0; i < 100; ++i) {
        spdlog::info("Warmup {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<pid_t> children;
    for (int p = 0; p < num_processes; ++p) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Fork 失败" << std::endl;
            continue;
        } else if (pid == 0) {
            // 子进程 - EnableProducer 会自动清理继承自父进程的状态
            spdlog::EnableProducer(spdlog::ProducerConfig(shm_handle));
            
            if (onep_format) {
                spdlog::SetProcessName("P" + std::to_string(p));
                spdlog::SetModuleName("Work");
            }
            
            for (int i = 0; i < messages_per_process; ++i) {
                spdlog::info("Process {} message {}", p, i);
            }
            
            // flush确保数据写入共享内存
            spdlog::default_logger()->flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            
            // 使用_exit()直接退出，不调用任何析构函数或atexit处理器
            // 这是fork后子进程最安全的退出方式
            _Exit(0);
        } else {
            children.push_back(pid);
        }
    }
    
    for (pid_t pid : children) {
        int status;
        waitpid(pid, &status, 0);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    int total_messages = num_processes * messages_per_process;
    double seconds = duration.count() / 1000000.0;
    double throughput = total_messages / seconds;
    
    std::cout << "总消息数: " << format_number(total_messages) << std::endl;
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
    std::cout << "吞吐量: " << format_number(throughput) << " msg/sec" << std::endl;
    
    // 等待消费者处理完所有消息
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    consumer->stop();
    spdlog::drop_all();
    spdlog::shutdown();
}

// 综合对比测试
void run_comparison_benchmark() {
    const int NUM_MESSAGES = 100000;
    const int NUM_THREADS = 4;
    const int MESSAGES_PER_THREAD = 25000;
    const int NUM_PROCESSES = 4;
    const int MESSAGES_PER_PROCESS = 25000;
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  性能对比测试：原版 spdlog vs spdlog-mp" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    // ========== 第一部分：原版 spdlog ==========
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "  第一部分：原版 spdlog 基准测试" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    bench_original_spdlog_sync(NUM_MESSAGES);
    bench_original_spdlog_async(NUM_MESSAGES);
    bench_original_spdlog_multithread(NUM_THREADS, MESSAGES_PER_THREAD);
    
    // ========== 第二部分：spdlog-mp 同步模式 ==========
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "  第二部分：spdlog-mp 同步模式（标准格式）" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    bench_mp_single_process(NUM_MESSAGES, false, false);
    bench_mp_latency(10000, false, false);
    bench_mp_multithread(NUM_THREADS, MESSAGES_PER_THREAD, false, false);
    bench_mp_multiprocess(NUM_PROCESSES, MESSAGES_PER_PROCESS, false, false);
    
    // ========== 第三部分：spdlog-mp 异步模式 ==========
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "  第三部分：spdlog-mp 异步模式（标准格式）" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    bench_mp_single_process(NUM_MESSAGES, true, false);
    bench_mp_latency(10000, true, false);
    bench_mp_multithread(NUM_THREADS, MESSAGES_PER_THREAD, true, false);
    bench_mp_multiprocess(NUM_PROCESSES, MESSAGES_PER_PROCESS, true, false);
    
    // ========== 第四部分：spdlog-mp OnePet格式 ==========
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "  第四部分：spdlog-mp 同步模式（OnePet格式）" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    bench_mp_single_process(NUM_MESSAGES, false, true);
    bench_mp_latency(10000, false, true);
    bench_mp_multithread(NUM_THREADS, MESSAGES_PER_THREAD, false, true);
    bench_mp_multiprocess(NUM_PROCESSES, MESSAGES_PER_PROCESS, false, true);
    
    // ========== 第五部分：spdlog-mp 异步+OnePet ==========
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "  第五部分：spdlog-mp 异步模式（OnePet格式）" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    bench_mp_single_process(NUM_MESSAGES, true, true);
    bench_mp_latency(10000, true, true);
    bench_mp_multithread(NUM_THREADS, MESSAGES_PER_THREAD, true, true);
    bench_mp_multiprocess(NUM_PROCESSES, MESSAGES_PER_PROCESS, true, true);
}

int main(int argc, char* argv[]) {
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  spdlog-mp 多进程日志性能基准测试" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "平台: " << 
#ifdef __APPLE__
        "macOS (Apple Silicon)"
#elif defined(__linux__)
        "Linux"
#else
        "Unknown"
#endif
        << std::endl;
    std::cout << "共享内存大小: " << SHM_SIZE / 1024 / 1024 << " MB" << std::endl;
    std::cout << "C++标准: C++17" << std::endl;
    
    run_comparison_benchmark();
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  测试完成" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    return 0;
}

#else

#include <iostream>
int main() {
    std::cout << "多进程支持未启用。请使用以下命令编译：" << std::endl;
    std::cout << "  xmake f --enable_multiprocess=y --build_bench=y" << std::endl;
    std::cout << "  xmake" << std::endl;
    return 1;
}

#endif
