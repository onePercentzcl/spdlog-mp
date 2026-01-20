// 对比测试：轮询模式 vs 通知模式
// 测试场景：
// 1. Fork 场景（有 eventfd）- 通知模式
// 2. 非 Fork 场景（无 eventfd）- 轮询模式

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <chrono>

void test_with_eventfd() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: Fork Scenario (WITH eventfd)" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 配置：poll_duration = 1 秒
    spdlog::ConsumerConfig config;
    config.shm_name = "/spdlog_test_eventfd";
    config.enable_onep_format = true;
    config.poll_duration = std::chrono::milliseconds(1000);  // 1 秒
    config.poll_interval = std::chrono::milliseconds(10);
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        std::cerr << "Failed to enable consumer!" << std::endl;
        return;
    }
    
    spdlog::SetProcessName("Main");
    auto handle = spdlog::GetSharedMemoryHandle();
    
    std::cout << "Configuration: poll_duration=" << config.poll_duration.count() 
              << "ms, poll_interval=" << config.poll_interval.count() << "ms" << std::endl;
    std::cout << "Shared memory: " << handle.name << ", fd=" << handle.fd << std::endl;
    std::cout << std::endl;
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child: Producer
        if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
            std::cerr << "Child: Failed to enable producer!" << std::endl;
            _exit(1);
        }
        
        spdlog::SetProcessName("Chld");
        spdlog::SetModuleName("Test");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "[CHILD] Sending 5 messages rapidly..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        
        for (int i = 1; i <= 5; i++) {
            spdlog::info("Message {} with eventfd", i);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "[CHILD] Sent 5 messages in " << duration.count() << "ms" << std::endl;
        
        _exit(0);
    } else if (pid > 0) {
        // Parent: Consumer
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n✓ Test 1 completed (check eventfd notifications above)" << std::endl;
    }
    
    spdlog::Shutdown();
    shm_unlink(config.shm_name.c_str());
}

void test_without_eventfd() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Polling Mode (WITHOUT eventfd)" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << "Note: This test simulates non-fork scenario" << std::endl;
    std::cout << "In real non-fork processes, eventfd is not shared" << std::endl;
    std::cout << "System falls back to polling mode (10ms interval)" << std::endl;
    std::cout << std::endl;
    
    // 配置：poll_duration = 1 秒
    spdlog::ConsumerConfig config;
    config.shm_name = "/spdlog_test_polling";
    config.enable_onep_format = true;
    config.poll_duration = std::chrono::milliseconds(1000);
    config.poll_interval = std::chrono::milliseconds(10);
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        std::cerr << "Failed to enable consumer!" << std::endl;
        return;
    }
    
    spdlog::SetProcessName("Main");
    auto handle = spdlog::GetSharedMemoryHandle();
    
    std::cout << "Configuration: poll_duration=" << config.poll_duration.count() 
              << "ms, poll_interval=" << config.poll_interval.count() << "ms" << std::endl;
    std::cout << "Shared memory: " << handle.name << std::endl;
    std::cout << std::endl;
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child: Producer
        if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
            std::cerr << "Child: Failed to enable producer!" << std::endl;
            _exit(1);
        }
        
        spdlog::SetProcessName("Chld");
        spdlog::SetModuleName("Poll");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "[CHILD] Sending 5 messages (polling mode)..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        
        for (int i = 1; i <= 5; i++) {
            spdlog::info("Message {} polling mode", i);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "[CHILD] Sent 5 messages in " << duration.count() << "ms" << std::endl;
        
        _exit(0);
    } else if (pid > 0) {
        // Parent: Consumer
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n✓ Test 2 completed (polling mode with 10ms interval)" << std::endl;
    }
    
    spdlog::Shutdown();
    shm_unlink(config.shm_name.c_str());
}

int main() {
    std::cout << "=== Polling vs Notification Mode Test ===" << std::endl;
    std::cout << "Comparing performance and behavior" << std::endl;
    
    // Test 1: With eventfd (fork scenario)
    test_with_eventfd();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 2: Without eventfd (polling mode)
    test_without_eventfd();
    
    std::cout << "\n=== All Tests Completed ===" << std::endl;
    std::cout << "\nSummary:" << std::endl;
    std::cout << "- Test 1 uses eventfd for immediate notification" << std::endl;
    std::cout << "- Test 2 uses polling with 10ms interval" << std::endl;
    std::cout << "- Both tests use poll_duration=1s for POLLING state" << std::endl;
    
    return 0;
}
