/*
 *@brief //main.h
 *@Proper
 *1.启动系统
 *2.创建共享内存
 *3.创建eventFD/UDS
 *4.创建日志器
 *5.启动子进程
 *6.启动主进程其它线程
 **/

#include "main.h"

#include <iostream>
#include <spdlog/multiprocess/custom_formatter.h>
#include <spdlog/spdlog.h>

#include "dpu/dpu.h"

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "onePet系统启动中..." << std::endl;

    //获取系统配置
    switch (onep::scu::LoadSystemConfig(systemConfig)) {
        case 1:
            std::cout << "获取系统配置失败！！！" << std::endl;
            return 1;
        case 0:
            std::cout << "获取系统配置成功" << std::endl;
        default: ;
    }

    //创建共享内存池
    switch (onep::scu::CreatShmPool(systemConfig)) {
        case 1:
            std::cout << "创建共享内存池失败！！！" << std::endl;

            return 1;
        case 2:
            std::cout << "设置共享内存池大小失败！！！" << std::endl;

            return 1;
        case 0:
            std::cout << "创建共享内存池成功" << std::endl;
        default: ;
    }

    //初始化日志
    switch (onep::scu::InitializeLogger(systemConfig)) {
        case 1:
            std::cout << "初始化消费者日志失败！！！" << std::endl;

            return 1;
        case 0:
            spdlog::info("日志初始化成功");
        default: ;
    }

    //初始化共享内存池
    switch (onep::scu::InitializeShmPool(systemConfig)) {
        case 1:
            spdlog::error("初始化共享内存池失败");
            spdlog::Shutdown();
            return 1;
        case 0: spdlog::info("共享内存池初始化成功");
        default: ;
    }

    //TODO：创建EventFD和UDS

    //TODO：创建系统监控线程

    //启动子进程
    //启动DPU子进程
    pid_t pid1 = fork();
    if (pid1 == 0) {
        onep::dpu::StartDPU(systemConfig);
        _exit(0);  // 子进程必须用 _exit() 退出
    } else if (pid1 < 0) {
        spdlog::error("Fork DPU 进程失败");
        return 1;
    }
    spdlog::info("Fork DPU 进程成功, PID: {}", pid1);

    // 等待子进程结束
    int status;
    waitpid(pid1, &status, 0);
    spdlog::info("DPU 进程已退出");

    spdlog::Shutdown();
    return 0;
}
