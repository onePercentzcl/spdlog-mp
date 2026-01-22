// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

/**
 * @file example_mp1_consumer.cpp
 * @brief 多进程日志示例1 - 消费者进程（主进程）
 * 
 * 演示功能：
 * - 非 fork 方式的多进程日志
 * - 8MB 共享内存，偏移 2MB 作为日志缓存区起始位置
 * - 日志缓存区大小 1MB，单槽位大小 512 字节
 * - 轮询间隔 1ms，轮询时间 1s
 * - 启用 OnePet 格式
 * - 异步模式
 * - UDS 通知模式
 * 
 * 主进程（Main）包含 2 个线程：
 * - Entrance: 入口线程
 * - Test: 测试各种日志级别输出
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

// Entrance 线程：入口线程，输出启动信息
void entrance_thread() {
    spdlog::SetModuleName("Entran");  // 6字符限制
    
    spdlog::info("=== 消费者进程启动 ===");
    spdlog::info("共享内存: /example_mp1_shm (8MB)");
    spdlog::info("日志缓存区偏移: 2MB");
    spdlog::info("日志缓存区大小: 1MB");
    spdlog::info("槽位大小: 512 字节");
    spdlog::info("通知模式: UDS");
    spdlog::info("等待生产者进程连接...");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_running) {
            spdlog::debug("Entrance 心跳 #{}", ++count);
        }
    }
    
    spdlog::info("Entrance 线程退出");
}

// Test 线程：测试各种日志级别输出
void test_thread() {
    spdlog::SetModuleName("Test");
    
    // 等待一小段时间让 Entrance 先输出
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    spdlog::info("=== 开始测试各种日志级别 ===");
    
    // 测试所有日志级别
    spdlog::trace("这是 TRACE 级别日志 - 最详细的跟踪信息");
    spdlog::debug("这是 DEBUG 级别日志 - 调试信息");
    spdlog::info("这是 INFO 级别日志 - 一般信息");
    spdlog::warn("这是 WARN 级别日志 - 警告信息");
    spdlog::error("这是 ERROR 级别日志 - 错误信息");
    spdlog::critical("这是 CRITICAL 级别日志 - 严重错误");
    
    // 测试格式化输出
    spdlog::info("格式化测试: 整数={}, 浮点={:.2f}, 字符串={}", 42, 3.14159, "hello");
    
    // 测试长消息
    std::string long_msg(200, 'X');
    spdlog::info("长消息测试: {}", long_msg);
    
    spdlog::info("=== 日志级别测试完成 ===");
    
    int count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_running) {
            spdlog::info("Test 周期日志 #{}", ++count);
        }
    }
    
    spdlog::info("Test 线程退出");
}

int main() {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // 配置消费者
    spdlog::ConsumerConfig cfg;
    
    // 共享内存配置
    cfg.shm_name = "/example_mp1_shm";
    cfg.shm_size = 8 * 1024 * 1024;     // 8MB 共享内存
    cfg.create_shm = true;               // 创建新的共享内存
    cfg.shm_offset = 2 * 1024 * 1024;    // 偏移 2MB（注意：create_shm=true 时此参数无效，需要在 ring buffer 层面处理）
    
    // 日志输出配置
    cfg.log_dir = "logs/";
    cfg.log_name = "mp1";
    cfg.enable_rotating = true;
    cfg.max_file_size = 10 * 1024 * 1024;  // 10MB
    cfg.max_files = 5;
    
    // 缓冲区配置
    cfg.slot_size = 512;                 // 单槽位 512 字节
    // 日志缓存区大小 1MB，槽位数量 = 1MB / 512 = 2048
    // 但实际槽位数量由 ring buffer 根据可用空间自动计算
    
    // 轮询配置
    cfg.poll_interval = std::chrono::milliseconds(1);    // 轮询间隔 1ms
    cfg.poll_duration = std::chrono::milliseconds(1000); // 轮询时间 1s
    
    // 模式配置
    cfg.async_mode = true;               // 异步模式
    cfg.enable_onep_format = true;       // 启用 OnePet 格式
    
    // 通知模式配置
    cfg.notify_mode = spdlog::NotifyMode::UDS;
    cfg.uds_path = "/tmp/example_mp1.sock";  // 自定义 UDS 路径
    
    // 启用消费者
    auto consumer = spdlog::EnableConsumer(cfg);
    if (!consumer) {
        std::cerr << "启用消费者失败！" << std::endl;
        return 1;
    }
    
    // 设置进程名
    spdlog::SetProcessName("Main");
    
    // 启动线程
    std::thread t1(entrance_thread);
    std::thread t2(test_thread);
    
    // 等待线程结束
    t1.join();
    t2.join();
    
    spdlog::info("=== 消费者进程退出 ===");
    spdlog::Shutdown();
    
    return 0;
}
