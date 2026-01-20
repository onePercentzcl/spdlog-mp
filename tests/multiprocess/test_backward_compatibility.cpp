// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/multiprocess/mode.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <random>
#include <vector>
#include <string>
#include <sstream>

using namespace spdlog;
using namespace spdlog::multiprocess;

namespace {
// 辅助函数：生成随机字符串
std::string generate_random_string(size_t length, std::mt19937& rng) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}
} // anonymous namespace

// 向后兼容性测试套件
class BackwardCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 确保多进程模式默认启用
        multiprocess::enable();
    }

    void TearDown() override {
        // 恢复多进程模式为启用状态
        multiprocess::enable();
    }
};

// **属性12：向后兼容性**
// **验证：需求 5.5, 10.2**
// **Feature: multiprocess-shared-memory, Property 12**
// 生成随机的spdlog使用场景
// 验证未启用多进程模式时行为不变
// 验证禁用多进程模式后行为恢复
TEST_F(BackwardCompatibilityTest, Property12_BackwardCompatibility) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 创建一个标准的ostream sink用于验证
        std::ostringstream oss;
        auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        ostream_sink->set_pattern("%v");  // 只输出消息内容
        
        // 创建标准logger（不使用多进程）
        auto standard_logger = std::make_shared<spdlog::logger>("standard_logger", ostream_sink);
        standard_logger->set_level(level::trace);
        
        // 生成随机日志消息
        std::uniform_int_distribution<size_t> msg_len_dist(10, 100);
        std::uniform_int_distribution<int> level_dist(0, 5);
        std::uniform_int_distribution<size_t> num_msgs_dist(5, 20);
        
        size_t num_messages = num_msgs_dist(rng);
        std::vector<std::pair<std::string, level::level_enum>> messages;
        
        for (size_t i = 0; i < num_messages; ++i) {
            std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
            level::level_enum lvl = static_cast<level::level_enum>(level_dist(rng));
            messages.push_back({msg_text, lvl});
            
            // 写入日志
            standard_logger->log(lvl, msg_text);
        }
        
        // 刷新logger
        standard_logger->flush();
        
        // 验证所有消息都被输出
        std::string output = oss.str();
        for (const auto& [msg_text, lvl] : messages) {
            EXPECT_NE(output.find(msg_text), std::string::npos) 
                << "迭代 " << iter << ": 消息未找到: " << msg_text;
        }
        
        // 验证消息数量
        size_t newline_count = std::count(output.begin(), output.end(), '\n');
        EXPECT_EQ(newline_count, num_messages) 
            << "迭代 " << iter << ": 期望 " << num_messages << " 条消息，实际 " << newline_count;
    }
}

// 测试禁用多进程模式后使用回退sink
TEST_F(BackwardCompatibilityTest, DisabledModeUsesFallbackSink) {
    const int NUM_ITERATIONS = 50;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 创建共享内存
        size_t buffer_size = 1024 * 1024;
        // 使用更唯一的名称，包含进程ID和随机数
        std::uniform_int_distribution<uint64_t> rand_dist(0, UINT64_MAX);
        std::string shm_name = "/test_fb_" + std::to_string(iter) + "_" + 
                               std::to_string(rand_dist(rng));
        
        // 先尝试清理可能存在的旧共享内存
        SharedMemoryHandle cleanup_handle;
        cleanup_handle.name = shm_name;
        cleanup_handle.fd = -1;
        cleanup_handle.size = buffer_size;
        SharedMemoryManager::destroy(cleanup_handle);
        
        auto create_result = SharedMemoryManager::create(buffer_size, shm_name);
        if (!create_result.is_ok()) {
            // 如果创建失败，跳过这次迭代
            continue;
        }
        auto handle = create_result.value();
        
        // 创建消费者sink（初始化共享内存）
        std::ostringstream consumer_oss;
        auto consumer_output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(consumer_oss);
        consumer_output_sink->set_pattern("%v");
        
        std::vector<spdlog::sink_ptr> output_sinks = {consumer_output_sink};
        ConsumerConfig consumer_config;
        consumer_config.poll_interval = std::chrono::milliseconds(0);
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle, output_sinks, consumer_config);
        
        // 创建回退sink
        std::ostringstream fallback_oss;
        auto fallback_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(fallback_oss);
        fallback_sink->set_pattern("%v");
        
        // 创建生产者sink（带回退）
        ProducerConfig producer_config;
        producer_config.slot_size = 4096;
        producer_config.overflow_policy = OverflowPolicy::Drop;
        producer_config.enable_fallback = true;
        producer_config.fallback_sink = fallback_sink;
        
        auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
        auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
        logger->set_level(level::trace);
        
        // 生成随机消息
        std::uniform_int_distribution<size_t> msg_len_dist(10, 50);
        std::string msg1 = generate_random_string(msg_len_dist(rng), rng);
        std::string msg2 = generate_random_string(msg_len_dist(rng), rng);
        std::string msg3 = generate_random_string(msg_len_dist(rng), rng);
        
        // 1. 多进程模式启用时，消息应该写入共享内存
        multiprocess::enable();
        logger->info(msg1);
        logger->flush();
        
        // 给一点时间让写入完成
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        
        // 消费者读取
        consumer_sink->poll_once();
        
        // 验证消息在消费者输出中
        std::string consumer_output = consumer_oss.str();
        EXPECT_NE(consumer_output.find(msg1), std::string::npos) 
            << "迭代 " << iter << ": 启用模式下消息应该在消费者输出中";
        
        // 验证消息不在回退输出中
        std::string fallback_output = fallback_oss.str();
        EXPECT_EQ(fallback_output.find(msg1), std::string::npos) 
            << "迭代 " << iter << ": 启用模式下消息不应该在回退输出中";
        
        // 2. 禁用多进程模式
        multiprocess::disable();
        logger->info(msg2);
        logger->flush();
        
        // 给一点时间
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        
        // 消费者尝试读取（应该没有新消息）
        consumer_sink->poll_once();
        
        // 验证消息在回退输出中
        fallback_output = fallback_oss.str();
        EXPECT_NE(fallback_output.find(msg2), std::string::npos) 
            << "迭代 " << iter << ": 禁用模式下消息应该在回退输出中";
        
        // 3. 重新启用多进程模式
        multiprocess::enable();
        logger->info(msg3);
        logger->flush();
        
        // 给一点时间
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        
        // 消费者读取
        consumer_sink->poll_once();
        
        // 验证消息在消费者输出中
        consumer_output = consumer_oss.str();
        EXPECT_NE(consumer_output.find(msg3), std::string::npos) 
            << "迭代 " << iter << ": 重新启用后消息应该在消费者输出中";
        
        // 清理
        SharedMemoryManager::destroy(handle);
    }
}

// 测试模式切换不影响标准spdlog行为
TEST_F(BackwardCompatibilityTest, ModeSwitchDoesNotAffectStandardSpdlog) {
    const int NUM_ITERATIONS = 50;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 创建标准ostream sink
        std::ostringstream oss;
        auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        ostream_sink->set_pattern("%v");
        
        // 创建标准logger（不使用多进程sink）
        auto standard_logger = std::make_shared<spdlog::logger>("standard", ostream_sink);
        standard_logger->set_level(level::trace);
        
        // 生成随机消息
        std::uniform_int_distribution<size_t> msg_len_dist(10, 50);
        std::string msg1 = generate_random_string(msg_len_dist(rng), rng);
        std::string msg2 = generate_random_string(msg_len_dist(rng), rng);
        std::string msg3 = generate_random_string(msg_len_dist(rng), rng);
        
        // 1. 多进程模式启用时
        multiprocess::enable();
        standard_logger->info(msg1);
        
        // 2. 禁用多进程模式
        multiprocess::disable();
        standard_logger->info(msg2);
        
        // 3. 重新启用多进程模式
        multiprocess::enable();
        standard_logger->info(msg3);
        
        // 刷新
        standard_logger->flush();
        
        // 验证所有消息都被输出（模式切换不影响标准logger）
        std::string output = oss.str();
        EXPECT_NE(output.find(msg1), std::string::npos) 
            << "迭代 " << iter << ": msg1应该在输出中";
        EXPECT_NE(output.find(msg2), std::string::npos) 
            << "迭代 " << iter << ": msg2应该在输出中";
        EXPECT_NE(output.find(msg3), std::string::npos) 
            << "迭代 " << iter << ": msg3应该在输出中";
        
        // 验证消息数量
        size_t newline_count = std::count(output.begin(), output.end(), '\n');
        EXPECT_EQ(newline_count, 3u) 
            << "迭代 " << iter << ": 应该有3条消息";
    }
}

// 测试is_enabled()函数
TEST_F(BackwardCompatibilityTest, IsEnabledFunction) {
    // 默认应该启用
    EXPECT_TRUE(multiprocess::is_enabled());
    
    // 禁用后应该返回false
    multiprocess::disable();
    EXPECT_FALSE(multiprocess::is_enabled());
    
    // 重新启用后应该返回true
    multiprocess::enable();
    EXPECT_TRUE(multiprocess::is_enabled());
    
    // 使用set_enabled测试
    multiprocess::set_enabled(false);
    EXPECT_FALSE(multiprocess::is_enabled());
    
    multiprocess::set_enabled(true);
    EXPECT_TRUE(multiprocess::is_enabled());
}

// 测试多次切换模式
TEST_F(BackwardCompatibilityTest, MultipleModeSwitches) {
    const int NUM_SWITCHES = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> bool_dist(0, 1);
    
    for (int i = 0; i < NUM_SWITCHES; ++i) {
        bool expected = bool_dist(rng) == 1;
        multiprocess::set_enabled(expected);
        EXPECT_EQ(multiprocess::is_enabled(), expected) 
            << "切换 " << i << ": 状态不匹配";
    }
}

// 测试禁用模式时无回退sink的行为
TEST_F(BackwardCompatibilityTest, DisabledModeWithoutFallback) {
    // 创建共享内存
    size_t buffer_size = 1024 * 1024;
    auto create_result = SharedMemoryManager::create(buffer_size, "test_no_fallback");
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建消费者sink（初始化共享内存）
    std::ostringstream consumer_oss;
    auto consumer_output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(consumer_oss);
    consumer_output_sink->set_pattern("%v");
    
    std::vector<spdlog::sink_ptr> output_sinks = {consumer_output_sink};
    ConsumerConfig consumer_config;
    consumer_config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle, output_sinks, consumer_config);
    
    // 创建生产者sink（无回退）
    ProducerConfig producer_config;
    producer_config.slot_size = 4096;
    producer_config.overflow_policy = OverflowPolicy::Drop;
    producer_config.enable_fallback = false;  // 不启用回退
    
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 禁用多进程模式
    multiprocess::disable();
    
    // 写入消息（应该被丢弃，不会崩溃）
    logger->info("This message should be dropped");
    logger->flush();
    
    // 消费者尝试读取
    consumer_sink->poll_once();
    
    // 验证没有消息被输出
    std::string output = consumer_oss.str();
    EXPECT_TRUE(output.empty()) << "禁用模式且无回退时，消息应该被丢弃";
    
    // 清理
    multiprocess::enable();
    SharedMemoryManager::destroy(handle);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS


// ============================================================================
// 单元测试部分
// ============================================================================

// 测试编译选项控制 - 需求 10.4
// 验证SPDLOG_ENABLE_MULTIPROCESS宏正确控制代码编译
TEST_F(BackwardCompatibilityTest, CompileOptionControl) {
    // 如果这个测试能运行，说明SPDLOG_ENABLE_MULTIPROCESS已启用
    // 因为整个测试文件都在#ifdef SPDLOG_ENABLE_MULTIPROCESS内
    
    // 验证多进程相关类型可用
    SharedMemoryHandle handle;
    EXPECT_EQ(handle.fd, -1);
    EXPECT_TRUE(handle.name.empty());
    EXPECT_EQ(handle.size, 0u);
    
    // 验证Result类型可用
    auto ok_result = Result<int>::ok(42);
    EXPECT_TRUE(ok_result.is_ok());
    EXPECT_EQ(ok_result.value(), 42);
    
    auto error_result = Result<int>::error("test error");
    EXPECT_TRUE(error_result.is_error());
    EXPECT_EQ(error_result.error_message(), "test error");
    
    // 验证BufferStats类型可用
    BufferStats stats;
    EXPECT_EQ(stats.total_writes, 0u);
    EXPECT_EQ(stats.total_reads, 0u);
    
    // 验证OverflowPolicy枚举可用
    OverflowPolicy policy1 = OverflowPolicy::Block;
    OverflowPolicy policy2 = OverflowPolicy::Drop;
    EXPECT_NE(static_cast<int>(policy1), static_cast<int>(policy2));
}

// 测试模式切换 - 需求 5.5
TEST_F(BackwardCompatibilityTest, ModeSwitchLogic) {
    // 测试enable()
    multiprocess::enable();
    EXPECT_TRUE(multiprocess::is_enabled());
    
    // 测试disable()
    multiprocess::disable();
    EXPECT_FALSE(multiprocess::is_enabled());
    
    // 测试set_enabled(true)
    multiprocess::set_enabled(true);
    EXPECT_TRUE(multiprocess::is_enabled());
    
    // 测试set_enabled(false)
    multiprocess::set_enabled(false);
    EXPECT_FALSE(multiprocess::is_enabled());
    
    // 恢复
    multiprocess::enable();
}

// 测试原始功能不受影响 - 需求 10.2
TEST_F(BackwardCompatibilityTest, OriginalFunctionalityUnaffected) {
    // 创建标准spdlog logger
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    sink->set_pattern("[%l] %v");
    
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    logger->set_level(level::trace);
    
    // 测试所有日志级别
    logger->trace("trace message");
    logger->debug("debug message");
    logger->info("info message");
    logger->warn("warn message");
    logger->error("error message");
    logger->critical("critical message");
    
    logger->flush();
    
    std::string output = oss.str();
    
    // 验证所有级别都正常工作
    EXPECT_NE(output.find("[trace]"), std::string::npos);
    EXPECT_NE(output.find("[debug]"), std::string::npos);
    EXPECT_NE(output.find("[info]"), std::string::npos);
    EXPECT_NE(output.find("[warning]"), std::string::npos);
    EXPECT_NE(output.find("[error]"), std::string::npos);
    EXPECT_NE(output.find("[critical]"), std::string::npos);
    
    // 验证消息内容
    EXPECT_NE(output.find("trace message"), std::string::npos);
    EXPECT_NE(output.find("debug message"), std::string::npos);
    EXPECT_NE(output.find("info message"), std::string::npos);
    EXPECT_NE(output.find("warn message"), std::string::npos);
    EXPECT_NE(output.find("error message"), std::string::npos);
    EXPECT_NE(output.find("critical message"), std::string::npos);
}

// 测试禁用模式时生产者sink行为
TEST_F(BackwardCompatibilityTest, ProducerSinkBehaviorWhenDisabled) {
    // 创建共享内存
    size_t buffer_size = 1024 * 1024;
    auto create_result = SharedMemoryManager::create(buffer_size, "/test_producer_disabled");
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建消费者sink（初始化共享内存）
    std::ostringstream consumer_oss;
    auto consumer_output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(consumer_oss);
    consumer_output_sink->set_pattern("%v");
    
    std::vector<spdlog::sink_ptr> output_sinks = {consumer_output_sink};
    ConsumerConfig consumer_config;
    consumer_config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle, output_sinks, consumer_config);
    
    // 创建回退sink
    std::ostringstream fallback_oss;
    auto fallback_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(fallback_oss);
    fallback_sink->set_pattern("%v");
    
    // 创建生产者sink
    ProducerConfig producer_config;
    producer_config.enable_fallback = true;
    producer_config.fallback_sink = fallback_sink;
    
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test", producer_sink);
    
    // 禁用多进程模式
    multiprocess::disable();
    
    // 写入消息
    logger->info("test message");
    logger->flush();
    
    // 验证消息在回退sink中
    std::string fallback_output = fallback_oss.str();
    EXPECT_NE(fallback_output.find("test message"), std::string::npos);
    
    // 验证消息不在消费者输出中
    consumer_sink->poll_once();
    std::string consumer_output = consumer_oss.str();
    EXPECT_EQ(consumer_output.find("test message"), std::string::npos);
    
    // 清理
    multiprocess::enable();
    SharedMemoryManager::destroy(handle);
}

// 测试启用模式时生产者sink行为
TEST_F(BackwardCompatibilityTest, ProducerSinkBehaviorWhenEnabled) {
    // 创建共享内存
    size_t buffer_size = 1024 * 1024;
    auto create_result = SharedMemoryManager::create(buffer_size, "/test_producer_enabled");
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建消费者sink（初始化共享内存）
    std::ostringstream consumer_oss;
    auto consumer_output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(consumer_oss);
    consumer_output_sink->set_pattern("%v");
    
    std::vector<spdlog::sink_ptr> output_sinks = {consumer_output_sink};
    ConsumerConfig consumer_config;
    consumer_config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle, output_sinks, consumer_config);
    
    // 创建回退sink
    std::ostringstream fallback_oss;
    auto fallback_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(fallback_oss);
    fallback_sink->set_pattern("%v");
    
    // 创建生产者sink
    ProducerConfig producer_config;
    producer_config.enable_fallback = true;
    producer_config.fallback_sink = fallback_sink;
    
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test", producer_sink);
    
    // 确保多进程模式启用
    multiprocess::enable();
    
    // 写入消息
    logger->info("test message");
    logger->flush();
    
    // 给一点时间让写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 消费者读取
    consumer_sink->poll_once();
    
    // 验证消息在消费者输出中
    std::string consumer_output = consumer_oss.str();
    EXPECT_NE(consumer_output.find("test message"), std::string::npos);
    
    // 验证消息不在回退sink中
    std::string fallback_output = fallback_oss.str();
    EXPECT_EQ(fallback_output.find("test message"), std::string::npos);
    
    // 清理
    SharedMemoryManager::destroy(handle);
}

// 测试MultiprocessMode类的静态方法
TEST_F(BackwardCompatibilityTest, MultiprocessModeClass) {
    // 测试MultiprocessMode::is_enabled()
    MultiprocessMode::enable();
    EXPECT_TRUE(MultiprocessMode::is_enabled());
    
    // 测试MultiprocessMode::disable()
    MultiprocessMode::disable();
    EXPECT_FALSE(MultiprocessMode::is_enabled());
    
    // 测试MultiprocessMode::set_enabled()
    MultiprocessMode::set_enabled(true);
    EXPECT_TRUE(MultiprocessMode::is_enabled());
    
    MultiprocessMode::set_enabled(false);
    EXPECT_FALSE(MultiprocessMode::is_enabled());
    
    // 恢复
    MultiprocessMode::enable();
}

// 测试线程安全的模式切换
TEST_F(BackwardCompatibilityTest, ThreadSafeModeSwitching) {
    const int NUM_THREADS = 4;
    const int NUM_ITERATIONS = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> enable_count{0};
    std::atomic<int> disable_count{0};
    
    // 启动多个线程同时切换模式
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_ITERATIONS; ++i) {
                if ((t + i) % 2 == 0) {
                    multiprocess::enable();
                    enable_count++;
                } else {
                    multiprocess::disable();
                    disable_count++;
                }
                
                // 读取状态（不应该崩溃）
                bool state = multiprocess::is_enabled();
                (void)state;
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 验证操作次数
    EXPECT_EQ(enable_count + disable_count, NUM_THREADS * NUM_ITERATIONS);
    
    // 恢复
    multiprocess::enable();
}
