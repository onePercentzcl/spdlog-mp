#pragma once
#include <cstdint>
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace onep {
    class SystemConfig {
    public:
        const char *m_ShmName{};
        size_t m_ShmSize = 16 * 1024 * 1024;
        size_t m_LogShmOffset = 4 * 1024 * 1024;
        size_t m_LogShmSize = 4 * 1024 * 1024;
        size_t m_LogSlotSize = 512;
        bool m_EnableOnepFormat = true;
    };
}

namespace onep::scu {
    //加载系统配置
    uint8_t LoadSystemConfig(onep::SystemConfig &systemConfig);

    //创建共享内存池
    uint8_t CreatShmPool(const onep::SystemConfig &systemConfig);

    //初始化主进程日志
    uint8_t InitializeLogger(const onep::SystemConfig &systemConfig);

    uint8_t InitializeShmPool(const onep::SystemConfig &systemConfig);
}
