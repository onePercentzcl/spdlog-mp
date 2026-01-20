// 共享内存偏移量测试示例
// 演示：使用者自己创建共享内存，然后使用其中一部分区域进行日志
//
// 场景：用户可能有一块大的共享内存用于多种用途，日志只使用其中一部分
//
// 运行方式：
//   终端1: ./multiprocess_offset_example consumer  (先启动消费者)
//   终端2: ./multiprocess_offset_example producer  (再启动生产者)
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
#include <sys/mman.h>
#include <fcntl.h>

// 共享内存配置
static const char* SHM_NAME = "/spdlog_offset_demo";
static const size_t TOTAL_SHM_SIZE = 8 * 1024 * 1024;  // 总共8MB
static const size_t LOG_OFFSET = 1 * 1024 * 1024;       // 日志从1MB处开始
static const size_t LOG_SIZE = 4 * 1024 * 1024;         // 日志使用4MB

static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

// ============================================================================
// 消费者进程：创建共享内存，使用偏移量区域
// ============================================================================
int run_consumer() {
    std::cout << "=== 消费者进程（偏移量测试）===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << std::endl;
    
    // 1. 用户自己创建共享内存
    std::cout << "步骤1: 创建共享内存 " << SHM_NAME << std::endl;
    std::cout << "  总大小: " << TOTAL_SHM_SIZE / 1024 / 1024 << "MB" << std::endl;
    std::cout << "  日志偏移: " << LOG_OFFSET / 1024 / 1024 << "MB" << std::endl;
    std::cout << "  日志大小: " << LOG_SIZE / 1024 / 1024 << "MB" << std::endl;
    std::cout << std::endl;
    
    // 清理旧的共享内存
    shm_unlink(SHM_NAME);
    
    // 创建共享内存
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        std::cerr << "创建共享内存失败: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // 设置大小
    if (ftruncate(fd, TOTAL_SHM_SIZE) == -1) {
        std::cerr << "设置共享内存大小失败: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    // 映射共享内存（用于演示其他用途）
    void* shm_ptr = mmap(nullptr, TOTAL_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        std::cerr << "映射共享内存失败: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    // 在偏移量之前的区域写入一些数据（模拟其他用途）
    std::cout << "步骤2: 在偏移量之前写入用户数据" << std::endl;
    char* user_data = static_cast<char*>(shm_ptr);
    const char* magic = "USER_DATA_AREA";
    memcpy(user_data, magic, strlen(magic) + 1);
    std::cout << "  写入: \"" << magic << "\" 在偏移0处" << std::endl;
    std::cout << std::endl;
    
    // 2. 使用 create_shm=false 连接到已存在的共享内存
    // EnableConsumer() 会自动启动消费者线程
    std::cout << "步骤3: 使用EnableConsumer连接共享内存（带偏移量）" << std::endl;
    
    spdlog::ConsumerConfig cfg;
    cfg.shm_name = SHM_NAME;
    cfg.shm_size = TOTAL_SHM_SIZE;
    cfg.create_shm = false;                                    // 连接已存在的共享内存
    cfg.shm_offset = LOG_OFFSET;                               // 从1MB处开始
    cfg.enable_onep_format = true;                             // 启用 onepFormat
    cfg.log_file = "logs/offset_demo.txt";
    cfg.slot_size = 4096;
    cfg.poll_interval = std::chrono::milliseconds(10);
    
    auto consumer = spdlog::EnableConsumer(cfg);
    
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        munmap(shm_ptr, TOTAL_SHM_SIZE);
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    spdlog::SetProcessName("Cons");
    spdlog::SetModuleName("Main");
    
    std::cout << "消费者已启动" << std::endl;
    std::cout << "按 Ctrl+C 退出" << std::endl;
    std::cout << std::endl;
    
    spdlog::info("消费者启动 (PID: {})", getpid());
    spdlog::info("使用偏移量: {}MB", LOG_OFFSET / 1024 / 1024);
    
    // 验证用户数据区域未被破坏
    std::cout << "步骤4: 验证用户数据区域" << std::endl;
    std::cout << "  读取偏移0处: \"" << user_data << "\"" << std::endl;
    if (strcmp(user_data, magic) == 0) {
        std::cout << "  ✓ 用户数据完整" << std::endl;
        spdlog::info("用户数据区域验证通过");
    } else {
        std::cout << "  ✗ 用户数据被破坏!" << std::endl;
        spdlog::error("用户数据区域被破坏!");
    }
    std::cout << std::endl;
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        count++;
        spdlog::info("消费者心跳 #{}", count);
        
        // 定期验证用户数据
        if (strcmp(user_data, magic) != 0) {
            spdlog::error("用户数据被破坏!");
        }
    }
    
    spdlog::info("消费者退出");
    std::cout << std::endl << "正在关闭..." << std::endl;
    
    spdlog::Shutdown();
    
    // 用户自己清理共享内存
    munmap(shm_ptr, TOTAL_SHM_SIZE);
    close(fd);
    shm_unlink(SHM_NAME);
    
    std::cout << "共享内存已清理" << std::endl;
    std::cout << "日志已保存到: logs/offset_demo.txt" << std::endl;
    return 0;
}

// ============================================================================
// 生产者进程：连接到共享内存，使用相同偏移量
// ============================================================================
int run_producer() {
    std::cout << "=== 生产者进程（偏移量测试）===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << std::endl;
    
    // 使用配置结构体（独立进程场景，带偏移量）
    spdlog::ProducerConfig cfg(SHM_NAME, TOTAL_SHM_SIZE);
    cfg.slot_size = 4096;
    cfg.shm_offset = LOG_OFFSET;                               // 必须与消费者相同
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    cfg.block_timeout = std::chrono::milliseconds(5000);
    
    if (!spdlog::EnableProducer(cfg)) {
        std::cerr << "连接共享内存失败！请先启动消费者进程。" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Prod");
    spdlog::SetModuleName("Main");
    
    std::cout << "已连接到共享内存: " << SHM_NAME << std::endl;
    std::cout << "使用偏移量: " << LOG_OFFSET / 1024 / 1024 << "MB" << std::endl;
    std::cout << std::endl;
    
    spdlog::info("生产者启动 (PID: {})", getpid());
    spdlog::info("使用偏移量: {}MB", LOG_OFFSET / 1024 / 1024);
    
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
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    
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
        std::cout << "共享内存偏移量测试示例" << std::endl;
        std::cout << std::endl;
        std::cout << "用法: " << argv[0] << " <consumer|producer>" << std::endl;
        std::cout << std::endl;
        std::cout << "此示例演示如何使用共享内存的一部分区域进行日志：" << std::endl;
        std::cout << "  - 总共享内存: " << TOTAL_SHM_SIZE / 1024 / 1024 << "MB" << std::endl;
        std::cout << "  - 日志偏移量: " << LOG_OFFSET / 1024 / 1024 << "MB" << std::endl;
        std::cout << "  - 日志区域大小: " << LOG_SIZE / 1024 / 1024 << "MB" << std::endl;
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
