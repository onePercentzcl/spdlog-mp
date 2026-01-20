// 独立进程共享内存日志示例
// 演示：消费者创建共享内存，生产者连接
//
// 运行方式：
//   终端1: ./multiprocess_separate_example consumer  (先启动消费者)
//   终端2: ./multiprocess_separate_example producer  (再启动生产者)
//
// 编译：需要启用 SPDLOG_ENABLE_MULTIPROCESS

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <atomic>

static const char* SHM_NAME = "/spdlog_separate_demo";
static const size_t SHM_SIZE = 4 * 1024 * 1024;  // 4MB
static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

// ============================================================================
// 消费者进程：创建共享内存并启动消费者
// ============================================================================
int run_consumer() {
    std::cout << "=== 消费者进程 ===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << std::endl;
    
    // 使用配置结构体
    // EnableConsumer() 会自动启动消费者线程
    spdlog::ConsumerConfig cfg;
    cfg.shm_name = SHM_NAME;
    cfg.shm_size = SHM_SIZE;
    cfg.log_file = "logs/separate_demo.txt";
    cfg.create_shm = true;                                     // 创建共享内存
    cfg.slot_size = 4096;                                      // 槽位大小
    cfg.poll_interval = std::chrono::milliseconds(10);         // 轮询间隔
    cfg.poll_duration = std::chrono::milliseconds(100);        // 轮询时间
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;       // 阻塞策略
    cfg.async_mode = false;                                    // 同步模式
    cfg.enable_onep_format = true;                             // 启用 onepFormat
    
    auto consumer = spdlog::EnableConsumer(cfg);
    
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Cons");
    spdlog::SetModuleName("Main");
    
    std::cout << "消费者已启动，等待生产者连接..." << std::endl;
    std::cout << "共享内存: " << SHM_NAME << std::endl;
    std::cout << "按 Ctrl+C 退出" << std::endl;
    std::cout << std::endl;
    
    spdlog::info("消费者启动 (PID: {})", getpid());
    spdlog::info("等待生产者连接...");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        count++;
        spdlog::info("消费者心跳 #{}", count);
    }
    
    spdlog::info("消费者退出");
    std::cout << std::endl << "正在关闭..." << std::endl;
    
    spdlog::Shutdown();
    
    std::cout << "日志已保存到: logs/separate_demo.txt" << std::endl;
    return 0;
}

// ============================================================================
// 生产者进程：连接到已存在的共享内存
// ============================================================================
int run_producer() {
    std::cout << "=== 生产者进程 ===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << std::endl;
    
    // 使用配置结构体（独立进程场景）
    spdlog::ProducerConfig cfg(SHM_NAME, SHM_SIZE);
    cfg.slot_size = 4096;
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    cfg.block_timeout = std::chrono::milliseconds(5000);
    
    if (!spdlog::EnableProducer(cfg)) {
        std::cerr << "连接共享内存失败！请先启动消费者进程。" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Prod");
    spdlog::SetModuleName("Main");
    
    std::cout << "已连接到共享内存: " << SHM_NAME << std::endl;
    std::cout << std::endl;
    
    spdlog::info("生产者启动 (PID: {})", getpid());
    
    // 写入各种级别的日志
    spdlog::trace("TRACE 日志");
    spdlog::debug("DEBUG 日志");
    spdlog::info("INFO 日志");
    spdlog::warn("WARN 日志");
    spdlog::error("ERROR 日志");
    spdlog::critical("CRITICAL 日志");
    
    // 模拟工作
    for (int i = 1; i <= 10; ++i) {
        spdlog::info("生产者工作进度: {}/10", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // 测试多线程
    spdlog::info("启动多线程测试...");
    
    std::vector<std::thread> threads;
    for (int t = 1; t <= 3; ++t) {
        threads.emplace_back([t]() {
            std::string module = "Thrd" + std::to_string(t);
            spdlog::SetModuleName(module);
            
            spdlog::info("线程 {} 启动", t);
            for (int i = 1; i <= 5; ++i) {
                spdlog::info("线程 {} 进度: {}/5", t, i);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            spdlog::info("线程 {} 完成", t);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    spdlog::SetModuleName("Main");
    spdlog::info("生产者完成工作");
    spdlog::info("生产者退出 (PID: {})", getpid());
    
    std::cout << std::endl << "生产者完成" << std::endl;
    return 0;
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <consumer|producer>" << std::endl;
        std::cout << std::endl;
        std::cout << "示例:" << std::endl;
        std::cout << "  终端1: " << argv[0] << " consumer  (先启动)" << std::endl;
        std::cout << "  终端2: " << argv[0] << " producer  (后启动)" << std::endl;
        return 1;
    }
    
    if (strcmp(argv[1], "consumer") == 0) {
        return run_consumer();
    } else if (strcmp(argv[1], "producer") == 0) {
        return run_producer();
    } else {
        std::cerr << "未知参数: " << argv[1] << std::endl;
        std::cerr << "请使用 'consumer' 或 'producer'" << std::endl;
        return 1;
    }
}

#else

#include <iostream>
int main() {
    std::cout << "多进程支持未启用。请使用 -DSPDLOG_ENABLE_MULTIPROCESS=ON 编译" << std::endl;
    return 1;
}

#endif
