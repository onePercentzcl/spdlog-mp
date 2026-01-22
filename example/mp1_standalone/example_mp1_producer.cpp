// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

/**
 * @file example_mp1_producer.cpp
 * @brief 多进程日志示例1 - 生产者进程
 * 
 * 演示功能：
 * - 非 fork 方式的多进程日志
 * - 连接到消费者创建的共享内存
 * - 异步模式
 * - UDS 通知模式
 * 
 * 生产者进程（Onep）包含 3 个线程：
 * - One: 测试生产者等待时发送日志，消费者是否能立即读内存并输出
 * - Two: 输出简单信息
 * - Three: 输出简单信息
 * 
 * 使用方法：
 * 1. 先启动消费者：./example_mp1_consumer
 * 2. 再启动生产者：./example_mp1_producer
 */

#include <spdlog/spdlog.h>
#include <spdlog/multiprocess/custom_formatter.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

// 全局退出标志
std::atomic<bool> g_running{true};

// 信号处理
void signal_handler(int) {
    g_running = false;
}

// One 线程：测试生产者等待时发送日志，消费者是否能立即响应
void one_thread() {
    spdlog::SetModuleName("One");
    
    spdlog::info("=== One 线程启动 ===");
    spdlog::info("测试：生产者等待时发送日志，消费者是否能立即响应");
    
    int count = 0;
    while (g_running && count < 20) {
        // 发送一条日志
        auto start = std::chrono::high_resolution_clock::now();
        spdlog::info("One 测试消息 #{} - 发送时间戳: {}", 
                     ++count, 
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         start.time_since_epoch()).count());
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        spdlog::debug("One 消息 #{} 发送耗时: {} 微秒", count, duration.count());
        
        // 等待一段时间，让消费者有时间进入等待状态
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    
    spdlog::info("One 线程完成测试，共发送 {} 条消息", count);
}

// Two 线程：输出简单信息
void two_thread() {
    spdlog::SetModuleName("Two");
    
    // 稍微延迟启动
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    spdlog::info("=== Two 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 10) {
        spdlog::info("Two 简单消息 #{}", ++count);
        spdlog::debug("Two 调试信息 #{}", count);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    spdlog::info("Two 线程退出");
}

// Three 线程：输出简单信息
void three_thread() {
    spdlog::SetModuleName("Three");
    
    // 稍微延迟启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    spdlog::info("=== Three 线程启动 ===");
    
    int count = 0;
    while (g_running && count < 10) {
        spdlog::info("Three 简单消息 #{}", ++count);
        spdlog::warn("Three 警告信息 #{}", count);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    
    spdlog::info("Three 线程退出");
}

int main() {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // 配置生产者
    spdlog::ProducerConfig cfg;
    
    // 共享内存配置（通过名称连接到消费者创建的共享内存）
    cfg.shm_name = "/example_mp1_shm";
    cfg.shm_size = 8 * 1024 * 1024;     // 8MB（需要与消费者一致）
    cfg.shm_offset = 0;                  // 偏移量（ring buffer 会从元数据读取实际配置）
    
    // 缓冲区配置
    cfg.slot_size = 512;                 // 单槽位 512 字节（需要与消费者一致）
    
    // 溢出策略
    cfg.overflow_policy = spdlog::OverflowPolicy::Block;
    cfg.block_timeout = std::chrono::milliseconds(5000);
    
    // 模式配置
    cfg.async_mode = true;               // 异步模式
    cfg.enable_onep_format = true;       // 启用 OnePet 格式
    
    // 通知模式配置（生产者会从共享内存元数据读取实际配置）
    cfg.notify_mode = spdlog::NotifyMode::UDS;
    cfg.uds_path = "/tmp/example_mp1.sock";  // 与消费者一致
    
    // 启用生产者
    if (!spdlog::EnableProducer(cfg)) {
        std::cerr << "启用生产者失败！请确保消费者进程已启动。" << std::endl;
        return 1;
    }
    
    // 设置进程名
    spdlog::SetProcessName("Onep");
    
    spdlog::info("=== 生产者进程启动 ===");
    spdlog::info("已连接到共享内存: /example_mp1_shm");
    
    // 启动线程
    std::thread t1(one_thread);
    std::thread t2(two_thread);
    std::thread t3(three_thread);
    
    // 等待线程结束
    t1.join();
    t2.join();
    t3.join();
    
    spdlog::info("=== 生产者进程退出 ===");
    
    // 生产者不调用 Shutdown()，让消费者负责清理
    // 只需要 flush 确保所有日志都发送出去
    spdlog::default_logger()->flush();
    
    return 0;
}
