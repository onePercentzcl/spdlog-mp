#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <chrono>

int main() {
    // 启用 onepFormat
    spdlog::ConsumerConfig config;
    config.enable_onep_format = true;
    config.log_name = "test_release";
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        spdlog::error("Failed to enable consumer!");
        return 1;
    }
    
    spdlog::SetProcessName("Main");
    spdlog::SetModuleName("Test");
    
    spdlog::info("Testing release mode format");
    spdlog::info("This should NOT show PID and Thread ID in release mode");
    
    auto handle = spdlog::GetSharedMemoryHandle();
    
    if (fork() == 0) {
        if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
            spdlog::error("Failed to enable producer!");
            _exit(1);
        }
        spdlog::SetProcessName("Son1");
        spdlog::SetModuleName("Child");
        
        for (int i = 1; i <= 3; ++i) {
            spdlog::info("Child log {}/3", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        _exit(0);
    }
    
    wait(nullptr);
    spdlog::info("Test completed!");
    spdlog::Shutdown();
    return 0;
}
