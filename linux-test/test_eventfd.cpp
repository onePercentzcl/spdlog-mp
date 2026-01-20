#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>

int main() {
    std::cout << "=== EventFD Fork Test ===" << std::endl;
    std::cout << "Testing eventfd behavior in fork scenario" << std::endl;
    std::cout << std::endl;
    
    // 启用 onepFormat 和 debug 模式
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
    
    // Fork 3 个子进程快速测试
    for (int i = 1; i <= 3; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程
            if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
                std::cerr << "Child " << i << ": Failed to enable producer!" << std::endl;
                _exit(1);
            }
            
            std::string child_name = "Ch" + std::to_string(i);
            spdlog::SetProcessName(child_name);
            spdlog::SetModuleName("Test");
            
            // 快速发送多条日志测试 eventfd 通知
            for (int j = 1; j <= 5; j++) {
                spdlog::info("Child {} message {}", i, j);
            }
            
            _exit(0);
        } else if (pid < 0) {
            std::cerr << "Fork failed!" << std::endl;
            return 1;
        }
        
        // 父进程：短暂延迟
        usleep(10000); // 10ms
    }
    
    spdlog::info("Parent: All children forked");
    
    // 等待所有子进程
    for (int i = 0; i < 3; i++) {
        int status;
        pid_t pid = wait(&status);
        std::cout << "Child " << pid << " exited with status " << status << std::endl;
    }
    
    spdlog::info("Parent: All children completed");
    std::cout << std::endl << "Test completed successfully!" << std::endl;
    
    spdlog::Shutdown();
    return 0;
}
