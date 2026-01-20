// 默认配置示例
// 演示：使用默认配置启动消费者和生产者
//
// 编译：需要启用 SPDLOG_ENABLE_MULTIPROCESS

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    std::cout << "=== 默认配置示例 ===" << std::endl;
    std::cout << "主进程 PID: " << getpid() << std::endl;
    std::cout << std::endl;
    
    // 使用默认配置启动消费者（无参数）
    // 默认配置：
    // - 共享内存名称: /spdlog_default_shm
    // - 共享内存大小: 4MB
    // - 日志目录: logs/
    // - 日志名称: app
    // - 日志文件: logs/YYYYMMDD_HHMMSS_app.log
    // - 日志轮转: 启用，单文件10MB，最多5个文件
    // - 轮询间隔: 10ms
    // - 轮询时间: 1s
    // - 槽位大小: 4096
    // - enable_onep_format: false（默认使用标准格式）
    // EnableConsumer() 会自动启动消费者线程
    spdlog::ConsumerConfig cfg;
    cfg.enable_onep_format = true;  // 启用 onepFormat
    auto consumer = spdlog::EnableConsumer(cfg);
    
    if (!consumer) {
        std::cerr << "创建消费者失败" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Main");
    spdlog::SetModuleName("Main");
    
    std::cout << "消费者已启动（使用默认配置）" << std::endl;
    std::cout << std::endl;
    
    // 获取共享内存句柄
    auto shm_handle = spdlog::GetSharedMemoryHandle();
    
    spdlog::info("主进程启动 (PID: {})", getpid());
    spdlog::info("使用默认共享内存: {}", shm_handle.name);
    
    // Fork一个子进程
    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("Fork失败");
        return 1;
    } else if (pid == 0) {
        // 子进程：使用父进程句柄启动生产者
        spdlog::EnableProducer(spdlog::ProducerConfig(shm_handle));
        
        spdlog::SetProcessName("Son1");
        spdlog::SetModuleName("Child");
        
        spdlog::info("子进程启动 (PID: {})", getpid());
        
        for (int i = 1; i <= 5; ++i) {
            spdlog::info("子进程进度: {}/5", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        spdlog::info("子进程完成");
        _exit(0);
    }
    
    spdlog::info("创建子进程 (PID: {})", pid);
    
    // 主进程工作
    for (int i = 1; i <= 3; ++i) {
        spdlog::info("主进程进度: {}/3", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    
    // 等待子进程
    int status;
    waitpid(pid, &status, 0);
    spdlog::info("子进程退出，状态: {}", WEXITSTATUS(status));
    
    spdlog::info("主进程退出");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    spdlog::Shutdown();
    
    std::cout << std::endl << "=== 完成 ===" << std::endl;
    std::cout << "日志文件保存在 logs/ 目录" << std::endl;
    
    return 0;
}

#else

#include <iostream>
int main() {
    std::cout << "多进程支持未启用。请使用 -DSPDLOG_ENABLE_MULTIPROCESS=ON 编译" << std::endl;
    return 1;
}

#endif
