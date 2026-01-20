// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/shared_memory_consumer_sink.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/logger.h>
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

// 基础测试套件
class ConsumerSinkPropertyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建共享内存用于测试
        buffer_size_ = 1024 * 1024; // 1MB
        auto create_result = SharedMemoryManager::create(buffer_size_, "test_consumer_props");
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

// **属性10-11：完整读取和日志级别支持**
// **验证：需求 4.3, 4.4, 5.4**
// **Feature: multiprocess-shared-memory, Property 10-11**
TEST_F(ConsumerSinkPropertyTest, Property10_11_CompleteReadAndLogLevelSupport) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        SharedMemoryManager::destroy(handle_);
        
        // 使用唯一的名称，避免冲突
        std::string shm_name = "test_props_" + std::to_string(iter) + "_" + 
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
        ASSERT_TRUE(create_result.is_ok()) << "迭代 " << iter << ": 创建共享内存失败: " << create_result.error_message();
        handle_ = create_result.value();
        
        // 创建输出流sink用于验证
        std::ostringstream oss;
        auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        output_sink->set_pattern("%v");  // 只输出消息内容
        
        // 创建消费者sink（会初始化共享内存）
        std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
        ConsumerConfig consumer_config;
        consumer_config.poll_interval = std::chrono::milliseconds(0);  // 不等待
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle_, output_sinks, consumer_config);
        
        // 创建生产者sink
        ProducerConfig producer_config;
        producer_config.slot_size = 4096;
        producer_config.overflow_policy = OverflowPolicy::Drop;
        
        auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
        auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
        logger->set_level(level::trace);  // 设置为最低级别，确保所有消息都被记录
        
        // 生成随机日志消息和级别
        std::uniform_int_distribution<size_t> msg_len_dist(10, 100);
        std::uniform_int_distribution<int> level_dist(0, 5);  // trace到critical
        
        std::vector<std::pair<std::string, level::level_enum>> messages;
        std::uniform_int_distribution<size_t> num_msgs_dist(5, 20);
        size_t num_messages = num_msgs_dist(rng);
        
        for (size_t i = 0; i < num_messages; ++i) {
            std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
            level::level_enum lvl = static_cast<level::level_enum>(level_dist(rng));
            messages.push_back({msg_text, lvl});
            
            // 写入日志
            logger->log(lvl, msg_text);
        }
        
        // 刷新logger确保所有消息都被写入
        logger->flush();
        
        // 直接检查环形缓冲区状态
        auto attach_result = SharedMemoryManager::attach(handle_);
        ASSERT_TRUE(attach_result.is_ok()) << "迭代 " << iter << ": attach失败";
        void* shm_ptr = attach_result.value();
        
        LockFreeRingBuffer check_buffer(shm_ptr, handle_.size, 4096, OverflowPolicy::Drop, false);
        auto stats = check_buffer.get_stats();
        
        // 验证写入数量
        EXPECT_EQ(stats.total_writes, num_messages) 
            << "迭代 " << iter << ": 写入数量不匹配，期望 " << num_messages 
            << "，实际 " << stats.total_writes
            << "，容量 " << stats.capacity;
        
        SharedMemoryManager::detach(shm_ptr, handle_.size);
        
        // 给一点时间让所有写入完成
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 消费者轮询读取所有消息
        // 循环等待直到所有消息都被读取或超时
        size_t messages_read = 0;
        int max_attempts = 100; // 最多尝试100次（约100ms）
        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            while (consumer_sink->poll_once()) {
                messages_read++;
            }
            if (messages_read >= num_messages) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // 验证输出的消息
        std::string output = oss.str();
        
        // 验证所有消息都被输出
        for (const auto& [msg_text, lvl] : messages) {
            EXPECT_NE(output.find(msg_text), std::string::npos) 
                << "Message not found in output: " << msg_text;
        }
        
        // 验证消息数量
        size_t newline_count = std::count(output.begin(), output.end(), '\n');
        EXPECT_EQ(newline_count, num_messages) 
            << "Expected " << num_messages << " messages, got " << newline_count;
    }
}

// 测试所有日志级别
TEST_F(ConsumerSinkPropertyTest, AllLogLevelsSupported) {
    // 创建输出流sink
    std::ostringstream oss;
    auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    output_sink->set_pattern("[%l] %v");
    
    // 创建消费者sink
    std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
    ConsumerConfig consumer_config;
    consumer_config.poll_interval = std::chrono::milliseconds(0);
    
    auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
        handle_, output_sinks, consumer_config);
    
    // 创建生产者sink
    ProducerConfig producer_config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    logger->set_level(level::trace);  // 设置为最低级别，确保所有消息都被记录
    
    // 测试所有日志级别
    logger->trace("trace message");
    logger->debug("debug message");
    logger->info("info message");
    logger->warn("warn message");
    logger->error("error message");
    logger->critical("critical message");
    
    // 刷新logger确保所有消息都被写入
    logger->flush();
    
    // 给一点时间让所有写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 消费者读取所有消息
    // 循环等待直到所有消息都被读取或超时
    int messages_read = 0;
    int max_attempts = 100; // 最多尝试100次（约100ms）
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        while (consumer_sink->poll_once()) {
            messages_read++;
        }
        if (messages_read >= 6) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 验证所有级别都被正确处理
    std::string output = oss.str();
    EXPECT_NE(output.find("[trace]"), std::string::npos);
    EXPECT_NE(output.find("[debug]"), std::string::npos);
    EXPECT_NE(output.find("[info]"), std::string::npos);
    EXPECT_NE(output.find("[warning]"), std::string::npos);
    EXPECT_NE(output.find("[error]"), std::string::npos);
    EXPECT_NE(output.find("[critical]"), std::string::npos);
}

// 测试消息包含所有必需字段
TEST_F(ConsumerSinkPropertyTest, MessageContainsAllFields) {
    const int NUM_ITERATIONS = 50;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化
        SharedMemoryManager::destroy(handle_);
        auto create_result = SharedMemoryManager::create(buffer_size_, "test_fields_" + std::to_string(iter));
        ASSERT_TRUE(create_result.is_ok());
        handle_ = create_result.value();
        
        // 创建输出sink
        std::ostringstream oss;
        auto output_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        output_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        
        std::vector<spdlog::sink_ptr> output_sinks = {output_sink};
        ConsumerConfig consumer_config;
        consumer_config.poll_interval = std::chrono::milliseconds(0);
        
        auto consumer_sink = std::make_unique<SharedMemoryConsumerSink>(
            handle_, output_sinks, consumer_config);
        
        // 创建生产者
        ProducerConfig producer_config;
        auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, producer_config);
        
        std::string logger_name = "test_logger_" + std::to_string(iter);
        auto logger = std::make_shared<spdlog::logger>(logger_name, producer_sink);
        
        // 生成随机消息
        std::uniform_int_distribution<size_t> msg_len_dist(20, 100);
        std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
        
        // 写入日志
        logger->info(msg_text);
        
        // 刷新logger确保消息被写入
        logger->flush();
        
        // 等待消息可用并读取（最多尝试100次）
        bool message_read = false;
        for (int attempt = 0; attempt < 100 && !message_read; ++attempt) {
            if (consumer_sink->poll_once()) {
                message_read = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        ASSERT_TRUE(message_read) << "迭代 " << iter << ": 未能读取消息";
        
        // 验证输出包含所有字段
        std::string output = oss.str();
        
        // 验证时间戳存在（格式：[YYYY-MM-DD HH:MM:SS.mmm]）
        EXPECT_TRUE(output.find('[') != std::string::npos);
        EXPECT_TRUE(output.find('-') != std::string::npos);
        EXPECT_TRUE(output.find(':') != std::string::npos);
        
        // 验证logger名称
        EXPECT_NE(output.find(logger_name), std::string::npos) 
            << "Logger name not found in output. Output: " << output << ", Expected logger: " << logger_name;
        
        // 验证日志级别
        EXPECT_NE(output.find("[info]"), std::string::npos);
        
        // 验证消息内容
        EXPECT_NE(output.find(msg_text), std::string::npos);
    }
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
