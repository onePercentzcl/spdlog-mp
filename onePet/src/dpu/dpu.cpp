#include "dpu/dpu.h"
#include <thread>
#include <chrono>

namespace onep::dpu {
    bool StartDPU(const SystemConfig &systemConfig) {
        InitializeLogger(systemConfig);
        
        // 子进程主循环
        spdlog::info("DPU 开始工作...");
        
        int count = 0;
        while (g_running && count < 5) {
            spdlog::info("DPU 工作中 #{}", ++count);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        spdlog::info("DPU 进程退出");
        spdlog::default_logger()->flush();
        
        return true;
    }

    bool InitializeLogger(const onep::SystemConfig &systemConfig) {
        spdlog::ProducerConfig cfg;
        cfg.shm_handle = handle;
        cfg.shm_offset = systemConfig.m_LogShmOffset;
        cfg.slot_size = systemConfig.m_LogSlotSize;
        cfg.overflow_policy = spdlog::OverflowPolicy::Block;
        cfg.block_timeout = std::chrono::milliseconds(5000);
        cfg.async_mode = false;
        cfg.enable_onep_format = true;
        cfg.notify_mode = spdlog::NotifyMode::EventFD;
        cfg.eventfd = eventFD;

        if (!spdlog::EnableProducer(cfg)) {
            fprintf(stderr, "DPU: 启用生产者失败\n");
            _exit(1);
        }

        spdlog::SetProcessName("DPU");
        spdlog::SetModuleName("Main");

        spdlog::info("DPU 进程启动成功");

        return true;
    }
}
