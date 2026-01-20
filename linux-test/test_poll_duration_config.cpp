// 测试 poll_duration 配置是否生效
// 验证：
// 1. 设置 poll_duration = 2 秒
// 2. 消费者进入 POLLING 状态后持续 2 秒
// 3. 2 秒内生产者跳过 eventfd 通知
// 4. 2 秒后生产者恢复 eventfd 通知

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== Poll Duration Configuration Test ===" << std::endl;
    std::cout << "Testing if poll_duration config takes effect" << std::endl;
    std::cout << std::endl;
    
    // 配置：设置 poll_duration = 2 秒（而不是默认的 1 秒或硬编码的 30 秒）
    spdlog::ConsumerConfig config;
    config.enable_onep_format = true;
    config.poll_duration = std::chrono::milliseconds(2000);  // 2 秒
    config.poll_interval = std::chrono::milliseconds(10);    // 10ms
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  poll_duration: " << config.poll_duration.count() << " ms" << std::endl;
    std::cout << "  poll_interval: " << config.poll_interval.count() << " ms" << std::endl;
    std::cout << std::endl;
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        std::cerr << "Failed to enable consumer!" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Main");
    auto handle = spdlog::GetSharedMemoryHandle();
    
    std::cout << "Parent process PID: " << getpid() << std::endl;
    std::cout << "Shared memory handle: name=" << handle.name << std::endl;
    std::cout << std::endl;
    
    spdlog::info("Parent: Before fork");
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process: Producer
        if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
            std::cerr << "Child: Failed to enable producer!" << std::endl;
            _exit(1);
        }
        
        spdlog::SetProcessName("Chld");
        spdlog::SetModuleName("Test");
        
        // 等待消费者准备好
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "[CHILD] Phase 1: Send first message (should trigger eventfd)" << std::endl;
        spdlog::info("Message 1 - Trigger POLLING state");
        
        // 等待消费者进入 POLLING 状态
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "[CHILD] Phase 2: Send messages within 2s POLLING period" << std::endl;
        for (int i = 2; i <= 5; i++) {
            spdlog::info("Message {} - Within POLLING (should skip eventfd)", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        
        std::cout << "[CHILD] Phase 3: Wait for POLLING to expire (2s)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << "[CHILD] Phase 4: Send message after POLLING expired" << std::endl;
        spdlog::info("Message 6 - After POLLING expired (should send eventfd)");
        
        std::cout << "[CHILD] Test complete" << std::endl;
        
        _exit(0);
    } else if (pid > 0) {
        // Parent process: Consumer
        spdlog::info("Parent: Child forked, waiting for messages...");
        
        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            spdlog::info("Parent: Child process completed successfully");
            std::cout << std::endl << "=== Test Result ===" << std::endl;
            std::cout << "✓ Check debug output above:" << std::endl;
            std::cout << "  1. Consumer should enter POLLING state with duration: 2.0s" << std::endl;
            std::cout << "  2. Producer should skip eventfd for messages 2-5" << std::endl;
            std::cout << "  3. Time remaining should decrease from ~2.0s to ~0.5s" << std::endl;
            std::cout << "  4. Message 6 should trigger eventfd after POLLING expired" << std::endl;
        } else {
            std::cerr << "Parent: Child process failed" << std::endl;
        }
        
    } else {
        std::cerr << "Fork failed!" << std::endl;
        return 1;
    }
    
    spdlog::Shutdown();
    return 0;
}
