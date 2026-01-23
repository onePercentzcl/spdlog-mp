
#include "scu/scu.h"
#include <main.h>

#include <sys/mman.h>
#include <sys/fcntl.h>
#include <iostream>

#include <cstring> // 用于 strerror
#include <cerrno>  // 用于 errno


namespace onep::scu {

    uint8_t LoadSystemConfig(SystemConfig &systemConfig) {
        systemConfig.m_ShmName = "/onep_shm";

        return 0;
    }

    uint8_t CreatShmPool(const SystemConfig &systemConfig) {
        // 先删除可能存在的旧共享内存
        shm_unlink(systemConfig.m_ShmName);

        //创建共享内存池
        g_ShmFD = shm_open(systemConfig.m_ShmName, O_CREAT | O_RDWR, 0666);
        if (g_ShmFD == -1) {
            return 1;
        }
        // 设置共享内存大小
        if (ftruncate(g_ShmFD, systemConfig.m_ShmSize) == -1) {
            std::cerr << "设置共享内存大小失败: " << strerror(errno) << std::endl;
            close(g_ShmFD);
            shm_unlink(systemConfig.m_ShmName);
            return 2;
        }

        return 0;
    }

    uint8_t InitializeLogger(const SystemConfig &systemConfig) {
        spdlog::ConsumerConfig cfg;
        cfg.shm_name = systemConfig.m_ShmName;
        cfg.enable_onep_format = systemConfig.m_EnableOnepFormat;
        cfg.shm_size = systemConfig.m_LogShmSize;
        cfg.shm_offset = systemConfig.m_LogShmOffset;
        cfg.slot_size = systemConfig.m_LogSlotSize;
        cfg.create_shm = false;
        cfg.log_name = "mp2";
        cfg.enable_rotating = true;
        cfg.max_file_size = 5*1024 * 1024;      // 5M
        cfg.max_files = 10;
        cfg.poll_interval = std::chrono::milliseconds(1);
        cfg.poll_duration = std::chrono::milliseconds(500);

        // 通知模式配置
        cfg.notify_mode = spdlog::NotifyMode::EventFD;


        // 模式配置
        cfg.async_mode = false;              // 消费者同步模式（避免 fork 后子进程继承异步线程池问题）
        cfg.enable_onep_format = true;

        // ========== 创建 EventFD（仅 Linux）==========

#ifdef __linux__
        eventFD = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
        if (eventFD == -1) {
            std::cerr << "创建 EventFD 失败，将使用 UDS 模式" << std::endl;
        } else {
            std::cout << "EventFD 创建成功: fd=" << eventFD << std::endl;
        }
#else
        std::cout << "非 Linux 系统，EventFD 不可用，将自动回退到 UDS 模式" << std::endl;
#endif

        // ========== 构建共享内存句柄 ==========

        handle.name = systemConfig.m_ShmName;
        handle.size = systemConfig.m_ShmSize;
        handle.fd = g_ShmFD;

        cfg.eventfd = eventFD;

        // 启用消费者
        auto consumer = spdlog::EnableConsumer(cfg);
        if (!consumer) {
            std::cerr << "启用消费者失败！" << std::endl;

            return 1;
        }
        spdlog::SetProcessName("SYST"); // 设置主进程名
        spdlog::SetModuleName("System");    // 设置当前模块名

        return 0;
    }

    uint8_t InitializeShmPool(const SystemConfig &systemConfig) {
        spdlog::info("正在初始化共享内存池{}", systemConfig.m_ShmName);
        return 0;
    }
}
