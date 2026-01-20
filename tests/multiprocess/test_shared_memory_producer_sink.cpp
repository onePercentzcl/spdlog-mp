// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/shared_memory_producer_sink.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/logger.h>

using namespace spdlog;
using namespace spdlog::multiprocess;

class SharedMemoryProducerSinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建共享内存用于测试
        buffer_size_ = 1024 * 1024; // 1MB
        auto create_result = SharedMemoryManager::create(buffer_size_, "test_producer_sink");
        ASSERT_TRUE(create_result.is_ok());
        handle_ = create_result.value();
        
        // 初始化共享内存（模拟消费者的初始化）
        auto attach_result = SharedMemoryManager::attach(handle_);
        ASSERT_TRUE(attach_result.is_ok());
        void* shm_ptr = attach_result.value();
        
        // 初始化环形缓冲区
        LockFreeRingBuffer buffer(shm_ptr, buffer_size_, 4096, OverflowPolicy::Drop, true);
        
        SharedMemoryManager::detach(shm_ptr, buffer_size_);
    }

    void TearDown() override {
        // 清理共享内存
        SharedMemoryManager::destroy(handle_);
    }

    SharedMemoryHandle handle_;
    size_t buffer_size_ = 0;
};

// 测试日志写入功能
TEST_F(SharedMemoryProducerSinkTest, BasicLogWriting) {
    // 创建生产者 sink
    ProducerConfig config;
    config.slot_size = 4096;
    config.overflow_policy = OverflowPolicy::Drop;
    
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, config);
    
    // 创建 logger
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入一些日志
    logger->info("Test message 1");
    logger->warn("Test message 2");
    logger->error("Test message 3");
    
    // 验证消息已写入（通过读取共享内存）
    auto attach_result = SharedMemoryManager::attach(handle_);
    ASSERT_TRUE(attach_result.is_ok());
    void* shm_ptr = attach_result.value();
    
    LockFreeRingBuffer buffer(shm_ptr, buffer_size_, 4096, OverflowPolicy::Drop, false);
    
    // 验证有3条消息
    int message_count = 0;
    while (buffer.is_next_slot_committed()) {
        std::vector<char> read_buffer(4096);
        auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
        ASSERT_TRUE(read_result.is_ok());
        
        buffer.release_slot();
        message_count++;
    }
    
    EXPECT_EQ(message_count, 3);
    
    SharedMemoryManager::detach(shm_ptr, buffer_size_);
}

// 测试缓冲区满时的错误处理
TEST_F(SharedMemoryProducerSinkTest, BufferFullHandling) {
    // 创建一个小的共享内存
    size_t small_size = 8192; // 8KB
    auto create_result = SharedMemoryManager::create(small_size, "test_small_buffer");
    ASSERT_TRUE(create_result.is_ok());
    auto small_handle = create_result.value();
    
    // 初始化
    auto attach_result = SharedMemoryManager::attach(small_handle);
    ASSERT_TRUE(attach_result.is_ok());
    void* shm_ptr = attach_result.value();
    LockFreeRingBuffer buffer(shm_ptr, small_size, 512, OverflowPolicy::Drop, true);
    SharedMemoryManager::detach(shm_ptr, small_size);
    
    // 创建生产者 sink
    ProducerConfig config;
    config.slot_size = 512;
    config.overflow_policy = OverflowPolicy::Drop;
    
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(small_handle, config);
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入大量日志直到缓冲区满
    for (int i = 0; i < 100; ++i) {
        logger->info("Message {}", i);
    }
    
    // 验证没有崩溃，消息被正确丢弃
    attach_result = SharedMemoryManager::attach(small_handle);
    ASSERT_TRUE(attach_result.is_ok());
    shm_ptr = attach_result.value();
    
    LockFreeRingBuffer read_buffer(shm_ptr, small_size, 512, OverflowPolicy::Drop, false);
    auto stats = read_buffer.get_stats();
    
    // 验证写入的消息数量不超过容量
    EXPECT_LE(stats.current_usage, stats.capacity);
    
    SharedMemoryManager::detach(shm_ptr, small_size);
    SharedMemoryManager::destroy(small_handle);
}

// 测试格式化保持一致性
TEST_F(SharedMemoryProducerSinkTest, FormattingConsistency) {
    // 创建生产者 sink
    ProducerConfig config;
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(handle_, config);
    
    // 设置自定义格式
    producer_sink->set_pattern("[%l] %v");
    
    auto logger = std::make_shared<spdlog::logger>("test_logger", producer_sink);
    
    // 写入日志
    logger->info("Formatted message");
    
    // 验证消息已写入
    auto attach_result = SharedMemoryManager::attach(handle_);
    ASSERT_TRUE(attach_result.is_ok());
    void* shm_ptr = attach_result.value();
    
    LockFreeRingBuffer buffer(shm_ptr, buffer_size_, 4096, OverflowPolicy::Drop, false);
    
    EXPECT_TRUE(buffer.is_next_slot_committed());
    
    std::vector<char> read_buffer(4096);
    auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
    ASSERT_TRUE(read_result.is_ok());
    
    // 验证消息内容
    auto* slot = reinterpret_cast<LockFreeRingBuffer::Slot*>(read_buffer.data());
    std::string received_msg(slot->payload, slot->length);
    EXPECT_EQ(received_msg, "Formatted message");
    
    SharedMemoryManager::detach(shm_ptr, buffer_size_);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
