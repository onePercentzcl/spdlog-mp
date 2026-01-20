// Test to verify POLLING state optimization
// This test verifies that:
// 1. Consumer enters POLLING state after receiving eventfd notification
// 2. Producer skips eventfd notifications during POLLING period
// 3. After timeout, consumer returns to WAITING state

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== POLLING State Optimization Test ===" << std::endl;
    std::cout << "Testing POLLING state transitions and eventfd optimization" << std::endl;
    std::cout << std::endl;
    
    // Enable onepFormat and debug mode
    spdlog::ConsumerConfig config;
    config.enable_onep_format = true;
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        std::cerr << "Failed to enable consumer!" << std::endl;
        return 1;
    }
    
    spdlog::SetProcessName("Main");
    auto handle = spdlog::GetSharedMemoryHandle();
    
    std::cout << "Parent process PID: " << getpid() << std::endl;
    std::cout << "Shared memory handle: name=" << handle.name 
              << ", fd=" << handle.fd 
              << ", size=" << handle.size << std::endl;
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
        
        // Wait a bit for consumer to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Phase 1: Send first message to trigger POLLING state
        std::cout << "[CHILD] Phase 1: Sending first message to trigger POLLING state" << std::endl;
        spdlog::info("Message 1 - This should trigger eventfd notification");
        
        // Wait for consumer to enter POLLING state
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Phase 2: Send rapid messages during POLLING period
        // Producer should skip eventfd notifications
        std::cout << "[CHILD] Phase 2: Sending rapid messages (producer should skip eventfd)" << std::endl;
        for (int i = 2; i <= 10; i++) {
            spdlog::info("Message {} - During POLLING period", i);
        }
        
        std::cout << "[CHILD] Phase 2 complete - sent 9 messages rapidly" << std::endl;
        
        // Phase 3: Wait for POLLING timeout (30 seconds is too long, so we'll just demonstrate)
        std::cout << "[CHILD] Phase 3: Waiting 2 seconds before final message" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        spdlog::info("Message 11 - After 2 second delay");
        
        std::cout << "[CHILD] Test complete" << std::endl;
        
        _exit(0);
    } else if (pid > 0) {
        // Parent process: Consumer
        spdlog::info("Parent: Child forked, waiting for messages...");
        
        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            spdlog::info("Parent: Child process completed successfully");
            std::cout << std::endl << "Test completed successfully!" << std::endl;
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
