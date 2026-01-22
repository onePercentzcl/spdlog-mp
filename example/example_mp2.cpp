// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

/**
 * @file example_mp2.cpp
 * @brief 多进程日志示例2 - Fork模式 + EventFD通知
 * 
 * 演示功能：
 * - 主进程创建共享内存和 EventFD
 * - 8MB 共享内存，偏移 4MB，日志缓存区 4MB
 * - 槽位大小 1024 字节
 * - EventFD 通知模式（macOS 自动回退到 UDS）
 * - 启用 OnePet 格式
 * - 日志保存至 ~/onep/ 目录
 * - 日志文件最大 128KB
 * - 消费者异步模式，生产者同步模式
 * - 主进程 fork 出 2 个子进程，每个进程 3 个线程
 * 
 * 进程/线程命名：
 * - 主进程 (Main): Consumer, Monitor, Heartbeat
 * - 子进程1 (Alfa): Alpha, Beta, Gamma
 * - 子进程2 (Brvo): Delta, Echo, Foxtrot
 */

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/eventfd.h>
#endif

// 全局退出标志
std::atomic<bool> g_running{true};

// 信号处理
void signal_handler(int) {
    g_running = false;
}

// ============================================================================
// 主进程线程
// ============================================================================

// Consumer 线程：消费者管理（实际消费由 ConsumerSink 自动处理）
void consumer_thread() {
    spdlog::SetModuleName("Consum");
    
    spdlog::info("=== Consumer 线程启动 ===");
    spdlog::info("消费者线程负责监控日志消费状态");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_running) {
            spdlog::debug("Consumer 状态检查 #{}", ++count);
        }
    }
    
    spdlog::info("Consumer 线程退出");
}

// Monitor 线程：系统监控
void monitor_thread() {
    spdlog::SetModuleName("Montor");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    spdlog::info("=== Monitor 线程启动 ===");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_running) {
            spdlog::info("Monitor 系统状态正常 #{}", ++count);
        }
    }
    
    spdlog::info("Monitor 线程退出");
}

// Heartbeat 线程：心跳
void heartbeat_thread() {
    spdlog::SetModuleName("Heartb");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    spdlog::info("=== Heartbeat 线程启动 ===");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_running) {
            spdlog::trace("Heartbeat #{}", ++count);
        }
    }
    
    spdlog::info("Heartbeat 线程退出");
}

// ============================================================================
// 子进程1 (Alfa) 线程
// ============================================================================

void alpha_thread() {
    spdlog::SetModuleName("Alpha");
    
    spdlog::info("=== Alpha 线程启动 ===");
    
    // 测试消息长度：槽位 1024 字节，头部约 100 字节，有效载荷约 900 字节
    // UTF-8 汉字每个 3 字节，理论上约 300 个汉字
    spdlog::info("=== 开始消息长度测试 ===");
    
    // 测试 100 个汉字
    std::string msg_100(100 * 3, '\0');
    for (int i = 0; i < 100; ++i) {
        msg_100[i*3] = '\xe4';     // "测" 的 UTF-8 编码
        msg_100[i*3+1] = '\xb8';
        msg_100[i*3+2] = '\xad';
    }
    spdlog::info("100汉字: {}", msg_100);
    
    // 测试 200 个汉字
    std::string msg_200(200 * 3, '\0');
    for (int i = 0; i < 200; ++i) {
        msg_200[i*3] = '\xe6';     // "试" 的 UTF-8 编码
        msg_200[i*3+1] = '\xb5';
        msg_200[i*3+2] = '\x8b';
    }
    spdlog::info("200汉字: {}", msg_200);
    
    // 测试 250 个汉字
    std::string msg_250(250 * 3, '\0');
    for (int i = 0; i < 250; ++i) {
        msg_250[i*3] = '\xe5';     // "字" 的 UTF-8 编码
        msg_250[i*3+1] = '\xad';
        msg_250[i*3+2] = '\x97';
    }
    spdlog::info("250汉字: {}", msg_250);
    
    // 测试 280 个汉字（接近极限）
    std::string msg_280(280 * 3, '\0');
    for (int i = 0; i < 280; ++i) {
        msg_280[i*3] = '\xe9';     // "限" 的 UTF-8 编码
        msg_280[i*3+1] = '\x99';
        msg_280[i*3+2] = '\x90';
    }
    spdlog::info("280汉字: {}", msg_280);
    
    spdlog::info("=== 消息长度测试完成 ===");
    
    int count = 0;
    while (g_running && count < 15) {
        spdlog::info("Alpha 数据处理 #{}", ++count);
        spdlog::debug("Alpha 详细信息 #{}", count);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
    
    spdlog::info("Alpha 线程完成");
}

void beta_thread() {
    spdlog::SetModuleName("Beta");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    
    spdlog::info("=== Beta 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 12) {
        spdlog::info("Beta 任务执行 #{}", ++count);
        if (count % 3 == 0) {
            spdlog::warn("Beta 警告: 任务 #{} 耗时较长", count);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    spdlog::info("Beta 线程完成");
}

void gamma_thread() {
    spdlog::SetModuleName("Gamma");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    spdlog::info("=== Gamma 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 10) {
        ++count;
        spdlog::info("Gamma 计算结果 #{}: {}", count, count * 3.14159);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }
    
    spdlog::info("Gamma 线程完成");
}

// ============================================================================
// 子进程2 (Brvo) 线程
// ============================================================================

void delta_thread() {
    spdlog::SetModuleName("Delta");
    
    spdlog::info("=== Delta 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 15) {
        spdlog::info("Delta 网络请求 #{}", ++count);
        if (count % 5 == 0) {
            spdlog::error("Delta 错误: 请求 #{} 超时", count);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }
    
    spdlog::info("Delta 线程完成");
}

void echo_thread() {
    spdlog::SetModuleName("Echo");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    
    spdlog::info("=== Echo 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 12) {
        spdlog::info("Echo 消息回显 #{}", ++count);
        spdlog::debug("Echo 消息内容: Message-{}", count);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
    }
    
    spdlog::info("Echo 线程完成");
}

void foxtrot_thread() {
    spdlog::SetModuleName("Foxtrt");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    
    spdlog::info("=== Foxtrot 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 8) {
        spdlog::info("Foxtrot 文件操作 #{}", ++count);
        if (count == 4) {
            spdlog::critical("Foxtrot 严重: 磁盘空间不足模拟");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    
    spdlog::info("Foxtrot 线程完成");
}

// ============================================================================
// 子进程入口
// ============================================================================

void run_child_process_alfa(const spdlog::SharedMemoryHandle& handle, int eventfd) {
    // fork 后子进程需要完全重新初始化 spdlog
    // 不能调用任何可能触发父进程 logger 析构的函数
    
    // 配置生产者
    spdlog::ProducerConfig cfg;
    cfg.shm_handle = handle;
    cfg.shm_offset = 4 * 1024 * 1024;    // 偏移 4MB
    cfg.slot_size = 1024;
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    cfg.block_timeout = std::chrono::milliseconds(5000);
    cfg.async_mode = false;              // 生产者同步模式
    cfg.enable_onep_format = true;
    cfg.notify_mode = spdlog::NotifyMode::EventFD;
    cfg.eventfd = eventfd;
    
    if (!spdlog::EnableProducer(cfg)) {
        fprintf(stderr, "Alfa: 启用生产者失败\n");
        _exit(1);
    }
    
    spdlog::SetProcessName("Alfa");
    spdlog::SetModuleName("Main");  // 主线程设置模块名
    spdlog::info("=== Alfa 进程启动 (PID: {}) ===", getpid());
    
    std::thread t1(alpha_thread);
    std::thread t2(beta_thread);
    std::thread t3(gamma_thread);
    
    t1.join();
    t2.join();
    t3.join();
    
    spdlog::SetModuleName("Main");  // 恢复主线程模块名
    spdlog::info("=== Alfa 进程退出 ===");
    spdlog::default_logger()->flush();
    
    _exit(0);
}

void run_child_process_brvo(const spdlog::SharedMemoryHandle& handle, int eventfd) {
    // fork 后子进程需要完全重新初始化 spdlog
    
    // 配置生产者
    spdlog::ProducerConfig cfg;
    cfg.shm_handle = handle;
    cfg.shm_offset = 4 * 1024 * 1024;    // 偏移 4MB
    cfg.slot_size = 1024;
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    cfg.block_timeout = std::chrono::milliseconds(5000);
    cfg.async_mode = false;              // 生产者同步模式
    cfg.enable_onep_format = true;
    cfg.notify_mode = spdlog::NotifyMode::EventFD;
    cfg.eventfd = eventfd;
    
    if (!spdlog::EnableProducer(cfg)) {
        fprintf(stderr, "Brvo: 启用生产者失败\n");
        _exit(1);
    }
    
    spdlog::SetProcessName("Brvo");
    spdlog::SetModuleName("Main");  // 主线程设置模块名
    spdlog::info("=== Brvo 进程启动 (PID: {}) ===", getpid());
    
    std::thread t1(delta_thread);
    std::thread t2(echo_thread);
    std::thread t3(foxtrot_thread);
    
    t1.join();
    t2.join();
    t3.join();
    
    spdlog::SetModuleName("Main");  // 恢复主线程模块名
    spdlog::info("=== Brvo 进程退出 ===");
    spdlog::default_logger()->flush();
    
    _exit(0);
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // ========== 原始方式创建共享内存 ==========
    const char* shm_name = "/example_mp2_shm";
    const size_t shm_size = 8 * 1024 * 1024;  // 8MB
    const size_t shm_offset = 4 * 1024 * 1024; // 偏移 4MB
    
    // 先删除可能存在的旧共享内存
    shm_unlink(shm_name);
    
    // 创建共享内存
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        std::cerr << "创建共享内存失败: " << strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "共享内存创建成功: " << shm_name << ", fd=" << shm_fd << std::endl;
    
    // 设置共享内存大小
    if (ftruncate(shm_fd, shm_size) == -1) {
        std::cerr << "设置共享内存大小失败: " << strerror(errno) << std::endl;
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }
    std::cout << "共享内存大小设置为: " << shm_size / (1024 * 1024) << "MB" << std::endl;
    
    // ========== 创建 EventFD（仅 Linux）==========
    int eventfd_fd = -1;
#ifdef __linux__
    eventfd_fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    if (eventfd_fd == -1) {
        std::cerr << "创建 EventFD 失败，将使用 UDS 模式" << std::endl;
    } else {
        std::cout << "EventFD 创建成功: fd=" << eventfd_fd << std::endl;
    }
#else
    std::cout << "非 Linux 系统，EventFD 不可用，将自动回退到 UDS 模式" << std::endl;
#endif
    
    // 展开 ~ 为实际路径
    const char* home = getenv("HOME");
    std::string log_dir = std::string(home ? home : "/tmp") + "/onep/";
    
    // ========== 构建共享内存句柄 ==========
    spdlog::SharedMemoryHandle handle;
    handle.name = shm_name;
    handle.size = shm_size;
    handle.fd = shm_fd;
    
    // ========== 配置消费者 ==========
    spdlog::ConsumerConfig cfg;
    
    // 共享内存配置 - 使用已创建的共享内存
    cfg.shm_name = shm_name;
    cfg.shm_size = shm_size;
    cfg.create_shm = false;              // 不创建，使用已存在的
    cfg.shm_offset = shm_offset;         // 偏移 4MB
    
    // 日志输出配置
    cfg.log_dir = log_dir;
    cfg.log_name = "mp2";
    cfg.enable_rotating = true;
    cfg.max_file_size = 128 * 1024;      // 128KB
    cfg.max_files = 10;
    
    // 缓冲区配置
    cfg.slot_size = 1024;                // 槽位 1024 字节
    
    // 轮询配置
    cfg.poll_interval = std::chrono::milliseconds(1);
    cfg.poll_duration = std::chrono::milliseconds(500);
    
    // 模式配置
    cfg.async_mode = false;              // 消费者同步模式（避免 fork 后子进程继承异步线程池问题）
    cfg.enable_onep_format = true;
    
    // 通知模式配置
    cfg.notify_mode = spdlog::NotifyMode::EventFD;
    cfg.eventfd = eventfd_fd;
    
    // 启用消费者
    auto consumer = spdlog::EnableConsumer(cfg);
    if (!consumer) {
        std::cerr << "启用消费者失败！" << std::endl;
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }
    
    // 设置进程名
    spdlog::SetProcessName("Main");
    spdlog::SetModuleName("Ctrl");  // 主线程：控制线程
    
    spdlog::info("=== 主进程启动 (PID: {}) ===", getpid());
    spdlog::info("共享内存: {} ({}MB)", shm_name, shm_size / (1024 * 1024));
    spdlog::info("日志缓存区偏移: {}MB", shm_offset / (1024 * 1024));
    spdlog::info("槽位大小: 1024 字节");
    spdlog::info("通知模式: EventFD (macOS 自动回退 UDS)");
    spdlog::info("日志目录: {}", log_dir);
    spdlog::info("日志文件最大: 128KB");
    
    // Fork 子进程1 (Alfa)
    pid_t pid1 = fork();
    if (pid1 == 0) {
        run_child_process_alfa(handle, eventfd_fd);
        // 不会到达这里
    } else if (pid1 < 0) {
        spdlog::error("Fork Alfa 进程失败");
        return 1;
    }
    spdlog::info("Fork Alfa 进程成功, PID: {}", pid1);
    
    // 稍微延迟再 fork 第二个子进程
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Fork 子进程2 (Brvo)
    pid_t pid2 = fork();
    if (pid2 == 0) {
        run_child_process_brvo(handle, eventfd_fd);
        // 不会到达这里
    } else if (pid2 < 0) {
        spdlog::error("Fork Brvo 进程失败");
        return 1;
    }
    spdlog::info("Fork Brvo 进程成功, PID: {}", pid2);
    
    // 启动主进程线程
    std::thread t1(consumer_thread);
    std::thread t2(monitor_thread);
    std::thread t3(heartbeat_thread);
    
    // 等待子进程结束
    int status;
    waitpid(pid1, &status, 0);
    spdlog::info("Alfa 进程已退出, 状态: {}", WEXITSTATUS(status));
    
    waitpid(pid2, &status, 0);
    spdlog::info("Brvo 进程已退出, 状态: {}", WEXITSTATUS(status));
    
    // 子进程都结束后，停止主进程
    spdlog::info("所有子进程已退出，准备关闭主进程");
    g_running = false;
    
    // 等待主进程线程结束
    t1.join();
    t2.join();
    t3.join();
    
    spdlog::info("=== 主进程退出 ===");
    spdlog::Shutdown();
    
    // 清理资源
#ifdef __linux__
    if (eventfd_fd >= 0) {
        close(eventfd_fd);
    }
#endif
    
    // 共享内存由 Shutdown() 中的 consumer_sink 析构函数清理
    // 但由于 create_shm=false，需要手动清理
    shm_unlink(shm_name);
    
    return 0;
}
