#pragma once
/**
* @file example_mp2.cpp
 * @brief 多进程日志示例2 - Fork模式 + EventFD通知
 *
 * 演示功能：
 * - 主进程创建共享内存和 EventFD
 * - 8MB 共享内存，偏移 4MB，日志缓存区 1MB（4MB-5MB）
 * - 槽位大小 1024 字节
 * - EventFD 通知模式（macOS 自动回退到 UDS）
 * - 启用 OnePet 格式
 * - 日志保存至 ~/onep/ 目录
 * - 日志文件最大 128KB
 * - 消费者异步模式，生产者同步模式
 * - 主进程 fork 出 2 个子进程，每个进程 3 个线程
 *
 * 进程/线程命名：
 * - 主进程 (Main): Consumer, Monitor, Heartbeat
 * - 子进程1 (Alfa): Alpha, Beta, Gamma
 * - 子进程2 (Brvo): Delta, Echo, Foxtrot
 */
#include <scu/scu.h>

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/eventfd.h>
#endif

// 全局退出标志
inline std::atomic<bool> g_running{true};

inline spdlog::SharedMemoryHandle handle;

inline int eventFD = -1;

// 信号处理
inline void signal_handler(int) {
    g_running = false;
}

inline onep::SystemConfig systemConfig;

inline int g_ShmFD = -1;

