// 多进程共享内存日志完整示例
// 演示：主进程fork出3个子进程，4个进程都使用spdlog写日志
//
// 简化API使用示例：
// - 消费者：spdlog::EnableConsumer("/shm_name", "log.txt", "Main")
// - 生产者：spdlog::EnableProducer(shm_handle, "Child")
//
// 编译：需要启用 SPDLOG_ENABLE_MULTIPROCESS
// 运行：./multiprocess_fork_example

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <atomic>

static const char* SHM_NAME = "/spdlog_fork_demo";
static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

// 子进程2的工作线程
void worker_thread(int id, const std::string& module) {
    spdlog::SetModuleName(module);
    spdlog::info("线程 {} ({}) 启动", id, module);
    for (int i = 1; i <= 5; ++i) {
        spdlog::info("线程 {} 工作进度: {}/5", id, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    spdlog::info("线程 {} ({}) 完成", id, module);
}

// 子进程逻辑
void run_child(int child_id, const spdlog::SharedMemoryHandle& shm_handle) {
    // 使用配置结构体启用生产者（fork场景）
    spdlog::ProducerConfig cfg(shm_handle);
    cfg.slot_size = 4096;
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    spdlog::EnableProducer(cfg);
    
    // 子进程3特殊处理：不设置进程名，测试NULL显示
    if (child_id != 3) {
        std::string name = "Son" + std::to_string(child_id);
        spdlog::SetProcessName(name);
        spdlog::SetModuleName("ChildP");
    }
    
    pid_t pid = getpid();
    spdlog::info("子进程 {} 启动 (PID: {})", child_id, pid);
    
    // 子进程2：多线程测试
    if (child_id == 2) {
        spdlog::info("子进程 {} 创建3个工作线程", child_id);
        
        std::vector<std::thread> threads;
        threads.emplace_back(worker_thread, 1, "Vision");
        threads.emplace_back(worker_thread, 2, "Audio");
        threads.emplace_back(worker_thread, 3, "Net");
        
        spdlog::SetModuleName("Main");
        for (int i = 1; i <= 3; ++i) {
            spdlog::info("子进程 {} 主线程进度: {}/3", child_id, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        
        for (auto& t : threads) t.join();
        spdlog::info("子进程 {} 完成 (PID: {})", child_id, pid);
        return;
    }
    
    // 子进程3：测试NULL显示（默认不设置进程名和模块名）
    if (child_id == 3) {
        // 不调用SetProcessName和SetModuleName，默认显示NULL
        spdlog::info("测试NULL显示（未设置进程名和模块名）");
        spdlog::warn("进程名和模块名应显示为NULL");
        
        spdlog::SetProcessName("Son3");
        spdlog::info("只设置进程名，模块名仍为NULL");
        
        spdlog::SetModuleName("Test");
        spdlog::info("进程名和模块名都已设置");
        
        // 慢速日志测试
        for (int i = 1; i <= 3; ++i) {
            spdlog::info("慢速日志 {}/3", i);
            std::this_thread::sleep_for(std::chrono::seconds(6));
        }
        spdlog::info("子进程 {} 完成 (PID: {})", child_id, pid);
        return;
    }
    
    // 子进程1：测试各种日志级别
    spdlog::trace("TRACE 日志");
    spdlog::debug("DEBUG 日志");
    spdlog::info("INFO 日志");
    spdlog::warn("WARN 日志");
    spdlog::error("ERROR 日志");
    spdlog::critical("CRITICAL 日志");
    
    for (int i = 1; i <= 10; ++i) {
        spdlog::info("子进程 {} 进度: {}/10", child_id, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    spdlog::info("子进程 {} 完成 (PID: {})", child_id, pid);
}

int main() {
    std::cout << "=== 多进程共享内存日志示例 ===" << std::endl;
    std::cout << "主进程 PID: " << getpid() << std::endl << std::endl;
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // 清理旧的共享内存
    shm_unlink(SHM_NAME);
    
    // ========================================
    // 使用配置结构体启用消费者
    // EnableConsumer() 会自动启动消费者线程
    // ========================================
    spdlog::ConsumerConfig cfg;
    cfg.shm_name = SHM_NAME;
    cfg.log_file = "logs/multiprocess_fork_demo.txt";
    cfg.create_shm = true;
    cfg.slot_size = 4096;
    cfg.poll_interval = std::chrono::milliseconds(10);
    cfg.poll_duration = std::chrono::milliseconds(100);
    
    auto consumer = spdlog::EnableConsumer(cfg);
    
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return 1;
    }
    
    // 单独设置进程名和模块名
    spdlog::SetProcessName("Main");
    spdlog::SetModuleName("Main");
    
    std::cout << "消费者已启动" << std::endl << std::endl;
    
    // 获取共享内存句柄（供子进程使用）
    auto shm_handle = spdlog::GetSharedMemoryHandle();
    
    spdlog::info("主进程启动 (PID: {})", getpid());
    spdlog::info("准备fork 3 个子进程");
    
    // Fork子进程
    std::vector<pid_t> children;
    for (int i = 1; i <= 3; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            spdlog::error("Fork失败");
        } else if (pid == 0) {
            run_child(i, shm_handle);
            _exit(0);
        } else {
            children.push_back(pid);
            spdlog::info("创建子进程 {} (PID: {})", i, pid);
        }
    }
    
    spdlog::info("所有子进程已创建");
    
    for (int i = 1; i <= 5; ++i) {
        spdlog::info("主进程进度: {}/5", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    spdlog::info("等待子进程结束...");
    for (pid_t pid : children) {
        int status;
        waitpid(pid, &status, 0);
        spdlog::info("子进程 {} 退出，状态: {}", pid, WEXITSTATUS(status));
    }
    
    spdlog::info("所有子进程已结束");
    spdlog::info("主进程退出");
    
    std::cout << std::endl << "等待消费者处理剩余日志..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    spdlog::Shutdown();
    
    std::cout << std::endl << "=== 完成 ===" << std::endl;
    std::cout << "日志已保存到: logs/multiprocess_fork_demo.txt" << std::endl;
    
    return 0;
}

#else

#include <iostream>
int main() {
    std::cout << "多进程支持未启用。请使用 -DSPDLOG_ENABLE_MULTIPROCESS=ON 编译" << std::endl;
    return 1;
}

#endif
