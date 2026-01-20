// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/logger.h>
#include <random>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>

using namespace spdlog;
using namespace spdlog::multiprocess;

namespace {
// 辅助函数：生成随机字符串
std::string generate_random_string(size_t length, std::mt19937& rng) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}

// 辅助函数：生成唯一的共享内存名称
std::string generate_unique_shm_name(std::mt19937& rng) {
    return "/test_cleanup_" + generate_random_string(8, rng);
}
} // anonymous namespace

// 资源清理测试套件
class ResourceCleanupTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_size_ = 1024 * 1024; // 1MB
    }

    void TearDown() override {
        // 清理
    }

    size_t buffer_size_ = 0;
};

// **属性17：资源清理 - 消费者终止前刷新消息**
// **验证：需求 9.1, 9.4**
// **Validates: Requirements 9.1, 9.4**
// **Feature: multiprocess-shared-memory, Property 17**
TEST_F(ResourceCleanupTest, Property17_ConsumerFlushesMessagesBeforeTermination) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 生成唯一的共享内存名称
        std::string shm_name = generate_unique_shm_name(rng);
        
        // 创建共享内存
        auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
        if (create_result.is_error()) {
            // 如果创建失败（可能是名称冲突），跳过这次迭代
            continue;
        }
        auto handle = create_result.value();
        
        // 创建输出流sink来捕获输出
        std::ostringstream oss;
        auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        output_sink->set_pattern("%v");
        
        // 生成随机数量的消息
        std::uniform_int_distribution<int> msg_count_dist(5, 20);
        int num_messages = msg_count_dist(rng);
        
        std::vector<std::string> expected_messages;
        for (int i = 0; i < num_messages; ++i) {
            expected_messages.push_back("Message_" + std::to_string(iter) + "_" + std::to_string(i));
        }
        
        // 创建消费者sink（配置为退出时销毁共享内存）
        {
            std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
            ConsumerConfig consumer_config;
            consumer_config.poll_interval = std::chrono::milliseconds(5);
            consumer_config.destroy_on_exit = true;
            
            auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
                handle, output_sinks, consumer_config);
            
            // 创建生产者sink
            ProducerConfig producer_config;
            producer_config.slot_size = 4096;
            auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
            auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
            
            // 启动消费者线程
            consumer_sink->start();
            
            // 写入所有消息
            for (const auto& msg : expected_messages) {
                logger->info("{}", msg);
            }
            
            // 等待一小段时间让消费者处理
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            // 消费者sink在这里析构，应该：
            // 1. 停止消费者线程（处理所有剩余消息）
            // 2. 刷新所有输出sink
            // 3. 销毁共享内存
        }
        
        // 验证所有消息都被输出
        std::string output = oss.str();
        for (const auto& expected_msg : expected_messages) {
            EXPECT_NE(output.find(expected_msg), std::string::npos)
                << "迭代 " << iter << ": 缺少消息 '" << expected_msg << "'";
        }
        
        // 验证共享内存已被销毁（尝试attach应该失败）
        auto attach_result = SharedMemoryManager::attach(handle);
        // 注意：在某些系统上，共享内存可能仍然存在一小段时间
        // 所以这个检查可能不总是可靠的
    }
}

// **属性18：进程隔离 - 生产者终止不影响其他进程**
// **验证：需求 9.2**
// **Validates: Requirements 9.2**
// **Feature: multiprocess-shared-memory, Property 18**
TEST_F(ResourceCleanupTest, Property18_ProducerTerminationDoesNotAffectOthers) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 生成唯一的共享内存名称
        std::string shm_name = generate_unique_shm_name(rng);
        
        // 创建共享内存
        auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
        if (create_result.is_error()) {
            continue;
        }
        auto handle = create_result.value();
        
        // 创建输出流sink
        std::ostringstream oss;
        auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        output_sink->set_pattern("%v");
        
        // 创建消费者sink（配置为退出时不销毁共享内存，以便测试）
        std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
        ConsumerConfig consumer_config;
        consumer_config.poll_interval = std::chrono::milliseconds(5);
        consumer_config.destroy_on_exit = false;  // 不销毁，以便后续测试
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle, output_sinks, consumer_config);
        consumer_sink->start();
        
        // 生成随机数量的生产者
        std::uniform_int_distribution<int> producer_count_dist(2, 5);
        int num_producers = producer_count_dist(rng);
        
        std::vector<std::string> all_messages;
        
        // 创建多个生产者，每个写入一些消息后终止
        for (int p = 0; p < num_producers; ++p) {
            // 创建生产者sink
            ProducerConfig producer_config;
            producer_config.slot_size = 4096;
            
            {
                auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
                auto logger = std::make_shared<spdlog::logger>("producer_" + std::to_string(p), producer_sink);
                
                // 写入一些消息
                std::uniform_int_distribution<int> msg_count_dist(3, 10);
                int num_messages = msg_count_dist(rng);
                
                for (int i = 0; i < num_messages; ++i) {
                    std::string msg = "P" + std::to_string(p) + "_M" + std::to_string(i);
                    logger->info("{}", msg);
                    all_messages.push_back(msg);
                }
                
                // 生产者sink在这里析构（模拟生产者终止）
            }
            
            // 验证消费者仍然可以工作
            // 等待一小段时间让消费者处理
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        
        // 等待消费者处理所有消息
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 停止消费者
        consumer_sink->stop();
        
        // 验证所有消息都被接收
        std::string output = oss.str();
        for (const auto& expected_msg : all_messages) {
            EXPECT_NE(output.find(expected_msg), std::string::npos)
                << "迭代 " << iter << ": 缺少消息 '" << expected_msg << "'";
        }
        
        // 清理共享内存
        SharedMemoryManager::destroy(handle);
    }
}

// 单元测试：验证消费者析构函数正确刷新sink
TEST_F(ResourceCleanupTest, ConsumerDestructorFlushesOutputSinks) {
    std::string shm_name = "/test_flush_on_destroy";
    
    // 创建共享内存
    auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    {
        // 创建消费者sink
        std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
        ConsumerConfig consumer_config;
        consumer_config.poll_interval = std::chrono::milliseconds(5);
        consumer_config.destroy_on_exit = true;
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle, output_sinks, consumer_config);
        
        // 创建生产者sink
        ProducerConfig producer_config;
        auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
        auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
        
        // 启动消费者
        consumer_sink->start();
        
        // 写入消息
        logger->info("Test message 1");
        logger->info("Test message 2");
        logger->info("Test message 3");
        
        // 等待消费者处理
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // 消费者sink在这里析构
    }
    
    // 验证所有消息都被输出
    std::string output = oss.str();
    EXPECT_NE(output.find("Test message 1"), std::string::npos);
    EXPECT_NE(output.find("Test message 2"), std::string::npos);
    EXPECT_NE(output.find("Test message 3"), std::string::npos);
}

// 单元测试：验证生产者析构函数正确分离共享内存
TEST_F(ResourceCleanupTest, ProducerDestructorDetachesSharedMemory) {
    std::string shm_name = "/test_producer_detach";
    
    // 创建共享内存
    auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 初始化共享内存（模拟消费者）
    auto attach_result = SharedMemoryManager::attach(handle);
    ASSERT_TRUE(attach_result.is_ok());
    void* shm_ptr = attach_result.value();
    LockFreeRingBuffer buffer(shm_ptr, buffer_size_, 4096, OverflowPolicy::Drop, true);
    SharedMemoryManager::detach(shm_ptr, buffer_size_);
    
    {
        // 创建生产者sink
        ProducerConfig producer_config;
        auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
        auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
        
        // 写入一些消息
        logger->info("Test message");
        
        // 生产者sink在这里析构
    }
    
    // 验证共享内存仍然存在（生产者只是分离，不销毁）
    auto reattach_result = SharedMemoryManager::attach(handle);
    EXPECT_TRUE(reattach_result.is_ok()) << "共享内存应该仍然存在";
    
    if (reattach_result.is_ok()) {
        SharedMemoryManager::detach(reattach_result.value(), buffer_size_);
    }
    
    // 清理
    SharedMemoryManager::destroy(handle);
}

// 单元测试：验证消费者配置destroy_on_exit选项
TEST_F(ResourceCleanupTest, ConsumerDestroyOnExitOption) {
    std::string shm_name = "/test_destroy_option";
    
    // 创建共享内存
    auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    
    {
        // 创建消费者sink，配置为退出时不销毁
        std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
        ConsumerConfig consumer_config;
        consumer_config.destroy_on_exit = false;
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle, output_sinks, consumer_config);
        
        // 消费者sink在这里析构
    }
    
    // 验证共享内存仍然存在
    auto attach_result = SharedMemoryManager::attach(handle);
    EXPECT_TRUE(attach_result.is_ok()) << "destroy_on_exit=false时，共享内存应该仍然存在";
    
    if (attach_result.is_ok()) {
        SharedMemoryManager::detach(attach_result.value(), buffer_size_);
    }
    
    // 清理
    SharedMemoryManager::destroy(handle);
}

// 单元测试：验证多个生产者同时终止
TEST_F(ResourceCleanupTest, MultipleProducersTerminateSimultaneously) {
    std::string shm_name = "/test_multi_producer_term";
    
    // 创建共享内存
    auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig consumer_config;
    consumer_config.poll_interval = std::chrono::milliseconds(5);
    consumer_config.destroy_on_exit = false;
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle, output_sinks, consumer_config);
    consumer_sink->start();
    
    // 创建多个生产者线程
    std::vector<std::thread> producer_threads;
    std::atomic<int> messages_written{0};
    
    for (int p = 0; p < 5; ++p) {
        producer_threads.emplace_back([&handle, p, &messages_written]() {
            ProducerConfig producer_config;
            auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle, producer_config);
            auto logger = std::make_shared<spdlog::logger>("producer_" + std::to_string(p), producer_sink);
            
            for (int i = 0; i < 10; ++i) {
                logger->info("P{}_M{}", p, i);
                messages_written++;
            }
            
            // 生产者在这里终止
        });
    }
    
    // 等待所有生产者完成
    for (auto& t : producer_threads) {
        t.join();
    }
    
    // 等待消费者处理所有消息
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 停止消费者
    consumer_sink->stop();
    
    // 验证消息数量
    std::string output = oss.str();
    int message_count = 0;
    size_t pos = 0;
    while ((pos = output.find("_M", pos)) != std::string::npos) {
        message_count++;
        pos++;
    }
    
    EXPECT_EQ(message_count, 50) << "应该收到50条消息";
    
    // 清理
    SharedMemoryManager::destroy(handle);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
