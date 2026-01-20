#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    // 启用 onepFormat
    spdlog::ConsumerConfig config;
    config.enable_onep_format = true;
    
    auto consumer = spdlog::EnableConsumer(config);
    if (!consumer) {
        spdlog::error("Failed to enable consumer!");
        return 1;
    }
    
    spdlog::SetProcessName("Main");
    spdlog::info("spdlog-mp test - main process on Linux");
    
    auto handle = spdlog::GetSharedMemoryHandle();
    spdlog::info("Shared memory: name={}, fd={}, size={}", 
                 handle.name, handle.fd, handle.size);
    
    if (fork() == 0) {
        if (!spdlog::EnableProducer(spdlog::ProducerConfig(handle))) {
            spdlog::error("Failed to enable producer!");
            _exit(1);
        }
        spdlog::SetProcessName("Child1");
        spdlog::SetModuleName("TestModule");
        spdlog::info("spdlog-mp test - child process");
        spdlog::warn("This is a warning from child");
        spdlog::error("This is an error from child");
        _exit(0);
    }
    
    wait(nullptr);
    spdlog::info("Test completed!");
    spdlog::Shutdown();
    return 0;
}
