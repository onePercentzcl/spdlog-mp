// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/logger.h>
#include <sstream>
#include <thread>
#include <chrono>

using namespace spdlog;
using namespace spdlog::multiprocess;

class SharedMemoryConsumerSinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建共享内存用于测试
        buffer_size_ = 1024 * 1024; // 1MB
        auto create_result = SharedMemoryManager::create(buffer_size_, "test_consumer_sink");
        ASSERT_TRUE(create_result.is_ok());
        handle_ = create_result.value();
        
        // 不需要手动初始化，消费者sink会初始化
    }

    void TearDown() override {
        // 清理共享内存
        SharedMemoryManager::destroy(handle_);
    }

    SharedMemoryHandle handle_;
    size_t buffer_size_ = 0;
};

// 测试日志读取和输出功能
TEST_F(SharedMemoryConsumerSinkTest, BasicLogReadingAndOutput) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(0);  // 不等待，立即返回
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者sink
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入一些日志
    logger->info("Message 1");
    logger->info("Message 2");
    logger->info("Message 3");
    
    // 消费者轮询读取
    consumer_sink->poll_once();
    consumer_sink->poll_once();
    consumer_sink->poll_once();
    
    // 验证输出
    std::string output = oss.str();
    EXPECT_NE(output.find("Message 1"), std::string::npos);
    EXPECT_NE(output.find("Message 2"), std::string::npos);
    EXPECT_NE(output.find("Message 3"), std::string::npos);
}

// 测试轮询逻辑
TEST_F(SharedMemoryConsumerSinkTest, PollingLogic) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 先轮询（应该没有消息）
    consumer_sink->poll_once();
    EXPECT_TRUE(oss.str().empty());
    
    // 写入消息
    logger->info("Test message");
    
    // 再次轮询（应该读取到消息）
    consumer_sink->poll_once();
    EXPECT_FALSE(oss.str().empty());
    EXPECT_NE(oss.str().find("Test message"), std::string::npos);
    
    // 清空输出流
    oss.str("");
    oss.clear();
    
    // 再次轮询（应该没有新消息）
    consumer_sink->poll_once();
    EXPECT_TRUE(oss.str().empty());
}

// 测试空缓冲区处理
TEST_F(SharedMemoryConsumerSinkTest, EmptyBufferHandling) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 多次轮询空缓冲区（不应该崩溃）
    for (int i = 0; i < 100; ++i) {
        consumer_sink->poll_once();
    }
    
    // 验证没有输出
    EXPECT_TRUE(oss.str().empty());
}

// 测试启动和停止消费者线程
TEST_F(SharedMemoryConsumerSinkTest, StartStopConsumerThread) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(10);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 启动消费者线程
    consumer_sink->start();
    
    // 写入一些日志
    logger->info("Message 1");
    logger->info("Message 2");
    logger->info("Message 3");
    
    // 等待消费者线程处理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 停止消费者线程
    consumer_sink->stop();
    
    // 验证输出
    std::string output = oss.str();
    EXPECT_NE(output.find("Message 1"), std::string::npos);
    EXPECT_NE(output.find("Message 2"), std::string::npos);
    EXPECT_NE(output.find("Message 3"), std::string::npos);
}

// 测试多个输出sink
TEST_F(SharedMemoryConsumerSinkTest, MultipleOutputSinks) {
    // 创建两个输出流sink
    std::ostringstream oss1, oss2;
    auto output_sink1 = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss1);
    auto output_sink2 = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss2);
    
    output_sink1->set_pattern("%v");
    output_sink2->set_pattern("[%l] %v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink1, output_sink2};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入日志
    logger->info("Test message");
    
    // 消费者读取
    consumer_sink->poll_once();
    
    // 验证两个sink都收到了消息
    EXPECT_NE(oss1.str().find("Test message"), std::string::npos);
    EXPECT_NE(oss2.str().find("Test message"), std::string::npos);
    EXPECT_NE(oss2.str().find("[info]"), std::string::npos);
}

// 测试消费者优雅关闭（处理剩余消息）
TEST_F(SharedMemoryConsumerSinkTest, GracefulShutdown) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(50);  // 较长的轮询间隔
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 启动消费者线程
    consumer_sink->start();
    
    // 写入大量日志
    for (int i = 0; i < 10; ++i) {
        logger->info("Message {}", i);
    }
    
    // 立即停止（应该处理完所有剩余消息）
    consumer_sink->stop();
    
    // 验证所有消息都被处理
    std::string output = oss.str();
    for (int i = 0; i < 10; ++i) {
        std::string expected = "Message " + std::to_string(i);
        EXPECT_NE(output.find(expected), std::string::npos) 
            << "Missing message: " << expected;
    }
}

// 测试日志级别过滤
TEST_F(SharedMemoryConsumerSinkTest, LogLevelFiltering) {
    // 创建输出流sink，只接受error及以上级别
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("%v");
    output_sink->set_level(level::err);
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig config;
    config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, config);
    
    // 创建生产者
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入不同级别的日志
    logger->info("Info message");
    logger->warn("Warn message");
    logger->error("Error message");
    logger->critical("Critical message");
    
    // 消费者读取
    for (int i = 0; i < 4; ++i) {
        consumer_sink->poll_once();
    }
    
    // 验证只有error和critical被输出
    std::string output = oss.str();
    EXPECT_EQ(output.find("Info message"), std::string::npos);
    EXPECT_EQ(output.find("Warn message"), std::string::npos);
    EXPECT_NE(output.find("Error message"), std::string::npos);
    EXPECT_NE(output.find("Critical message"), std::string::npos);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
