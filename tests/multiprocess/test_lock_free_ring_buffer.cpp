// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/details/log_msg.h>
#include <random>
#include <vector>
#include <string>
#include <cstring>

using namespace spdlog;

// 基础测试套件
class LockFreeRingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 分配共享内存用于测试
        buffer_size_ = 1024 * 1024; // 1MB
        shared_memory_ = std::malloc(buffer_size_);
        ASSERT_NE(shared_memory_, nullptr);
    }

    void TearDown() override {
        if (shared_memory_) {
            std::free(shared_memory_);
            shared_memory_ = nullptr;
        }
    }
    
    // 辅助函数：创建测试用的log_msg
    details::log_msg create_test_log_msg(const std::string& message, 
                                         level::level_enum lvl = level::info,
                                         const std::string& logger_name = "test_logger") {
        return details::log_msg(
            log_clock::now(),
            source_loc{},
            string_view_t(logger_name),
            lvl,
            string_view_t(message)
        );
    }

    void* shared_memory_ = nullptr;
    size_t buffer_size_ = 0;
};

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

// **属性7-8：完整写入和溢出处理**
// **验证：需求 3.2, 3.3, 3.4, 3.6**
// **Feature: multiprocess-shared-memory, Property 7-8**
TEST_F(LockFreeRingBufferTest, Property7_8_CompleteWriteAndOverflowHandling) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        // 生成随机配置
        std::uniform_int_distribution<size_t> slot_size_dist(256, 2048);
        size_t slot_size = slot_size_dist(rng);
        
        // 测试丢弃策略
        {
            LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
            auto stats = buffer.get_stats();
            size_t capacity = stats.capacity;
            
            // 生成随机日志消息
            std::uniform_int_distribution<size_t> msg_len_dist(10, 200);
            std::vector<std::string> messages;
            
            // 写入直到缓冲区满
            size_t successful_writes = 0;
            for (size_t i = 0; i < capacity + 10; ++i) {
                std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
                messages.push_back(msg_text);
                
                // 创建log_msg
                auto log_msg = create_test_log_msg(msg_text);
                
                // 预留槽位
                auto reserve_result = buffer.reserve_slot();
                if (reserve_result.is_ok()) {
                    size_t slot_idx = reserve_result.value();
                    
                    // 写入数据
                    buffer.write_slot(slot_idx, log_msg);
                    
                    // 提交槽位
                    buffer.commit_slot(slot_idx);
                    
                    successful_writes++;
                    
                    // 验证只有提交后的消息可见
                    EXPECT_TRUE(buffer.is_next_slot_committed());
                } else {
                    // 缓冲区满，丢弃策略应该返回错误
                    EXPECT_EQ(reserve_result.error_message(), "Buffer is full, message dropped");
                    break;
                }
            }
            
            // 验证成功写入的数量不超过容量
            EXPECT_LE(successful_writes, capacity);
        }
        
        // 测试阻塞策略（简化版，不实际阻塞）
        {
            std::memset(shared_memory_, 0, buffer_size_);
            LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Block);
            auto stats = buffer.get_stats();
            size_t capacity = stats.capacity;
            
            std::uniform_int_distribution<size_t> msg_len_dist2(10, 200);
            
            // 写入一些消息
            for (size_t i = 0; i < capacity / 2; ++i) {
                std::string msg_text = generate_random_string(msg_len_dist2(rng), rng);
                auto log_msg = create_test_log_msg(msg_text);
                
                auto reserve_result = buffer.reserve_slot();
                ASSERT_TRUE(reserve_result.is_ok());
                
                size_t slot_idx = reserve_result.value();
                buffer.write_slot(slot_idx, log_msg);
                buffer.commit_slot(slot_idx);
            }
            
            // 验证消息可见
            for (size_t i = 0; i < capacity / 2; ++i) {
                EXPECT_TRUE(buffer.is_next_slot_committed());
                
                std::vector<char> read_buffer(slot_size);
                auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
                ASSERT_TRUE(read_result.is_ok());
                
                buffer.release_slot();
            }
        }
    }
}

// **属性9：消息顺序保证**
// **验证：需求 4.7**
// **Feature: multiprocess-shared-memory, Property 9**
TEST_F(LockFreeRingBufferTest, Property9_MessageOrderGuarantee) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        size_t slot_size = 512;
        LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
        
        // 生成随机消息序列
        std::uniform_int_distribution<size_t> num_msgs_dist(10, 50);
        size_t num_messages = num_msgs_dist(rng);
        
        std::vector<std::string> messages;
        for (size_t i = 0; i < num_messages; ++i) {
            messages.push_back("Message_" + std::to_string(i));
        }
        
        // 按顺序写入所有消息
        for (const auto& msg_text : messages) {
            auto log_msg = create_test_log_msg(msg_text);
            
            auto reserve_result = buffer.reserve_slot();
            if (!reserve_result.is_ok()) {
                break; // 缓冲区满
            }
            
            size_t slot_idx = reserve_result.value();
            buffer.write_slot(slot_idx, log_msg);
            buffer.commit_slot(slot_idx);
        }
        
        // 验证读取顺序与写入顺序一致
        size_t read_count = 0;
        while (buffer.is_next_slot_committed()) {
            std::vector<char> read_buffer(slot_size);
            auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
            ASSERT_TRUE(read_result.is_ok());
            
            // 从槽位中提取payload
            auto* slot = reinterpret_cast<LockFreeRingBuffer::Slot*>(read_buffer.data());
            std::string received_msg(slot->payload, slot->length);
            
            // 验证消息顺序
            EXPECT_EQ(received_msg, messages[read_count]);
            
            buffer.release_slot();
            read_count++;
        }
        
        // 验证读取的消息数量正确
        EXPECT_EQ(read_count, messages.size());
    }
}

// **属性13-14：消息完整性和提交标志**
// **验证：需求 6.3, 6.4**
// **Feature: multiprocess-shared-memory, Property 13-14**
TEST_F(LockFreeRingBufferTest, Property13_14_MessageIntegrityAndCommitFlag) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        size_t slot_size = 1024;
        LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
        
        // 生成随机消息
        std::uniform_int_distribution<size_t> msg_len_dist(50, 500);
        std::uniform_int_distribution<int> level_dist(0, 6);
        
        std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
        level::level_enum lvl = static_cast<level::level_enum>(level_dist(rng));
        std::string logger_name = "test_logger_" + std::to_string(iter);
        
        // 创建log_msg
        auto log_msg = create_test_log_msg(msg_text, lvl, logger_name);
        
        // 写入消息
        auto reserve_result = buffer.reserve_slot();
        ASSERT_TRUE(reserve_result.is_ok());
        
        size_t slot_idx = reserve_result.value();
        
        // 在提交前，验证提交标志为false
        EXPECT_FALSE(buffer.is_next_slot_committed());
        
        // 写入数据
        buffer.write_slot(slot_idx, log_msg);
        
        // 提交槽位
        buffer.commit_slot(slot_idx);
        
        // 验证提交标志正确设置
        EXPECT_TRUE(buffer.is_next_slot_committed());
        
        // 读取消息
        std::vector<char> read_buffer(slot_size);
        auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
        ASSERT_TRUE(read_result.is_ok());
        
        // 验证所有字段存在
        auto* slot = reinterpret_cast<LockFreeRingBuffer::Slot*>(read_buffer.data());
        
        // 验证长度字段
        EXPECT_GT(slot->length, 0u);
        EXPECT_EQ(slot->length, msg_text.size());
        
        // 验证时间戳字段
        EXPECT_GT(slot->timestamp, 0u);
        
        // 验证日志级别
        EXPECT_EQ(slot->level, static_cast<uint8_t>(lvl));
        
        // 验证logger名称
        EXPECT_EQ(std::string(slot->logger_name), logger_name);
        
        // 验证消息内容
        std::string received_msg(slot->payload, slot->length);
        EXPECT_EQ(received_msg, msg_text);
        
        buffer.release_slot();
    }
}

// **属性19：非阻塞写入**
// **验证：需求 11.5**
// **Feature: multiprocess-shared-memory, Property 19**
TEST_F(LockFreeRingBufferTest, Property19_NonBlockingWrite) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        // 生成随机配置
        std::uniform_int_distribution<size_t> slot_size_dist(256, 1024);
        size_t slot_size = slot_size_dist(rng);
        
        // 使用丢弃策略创建缓冲区（非阻塞）
        LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
        auto stats = buffer.get_stats();
        size_t capacity = stats.capacity;
        
        // 生成随机数量的写入操作
        std::uniform_int_distribution<size_t> num_writes_dist(1, capacity / 2);
        size_t num_writes = num_writes_dist(rng);
        
        // 测量写入延迟
        std::vector<std::chrono::nanoseconds> latencies;
        latencies.reserve(num_writes);
        
        std::uniform_int_distribution<size_t> msg_len_dist(10, 200);
        
        for (size_t i = 0; i < num_writes; ++i) {
            std::string msg_text = generate_random_string(msg_len_dist(rng), rng);
            auto log_msg = create_test_log_msg(msg_text);
            
            // 验证缓冲区未满
            ASSERT_FALSE(buffer.is_full()) << "Buffer should not be full at iteration " << i;
            
            // 测量写入延迟
            auto start = std::chrono::high_resolution_clock::now();
            
            // 使用try_reserve_slot进行非阻塞预留
            auto reserve_result = buffer.try_reserve_slot();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            latencies.push_back(latency);
            
            // 验证非阻塞：预留操作应该立即返回
            // 对于未满的缓冲区，应该成功
            ASSERT_TRUE(reserve_result.is_ok()) 
                << "try_reserve_slot should succeed when buffer is not full";
            
            size_t slot_idx = reserve_result.value();
            buffer.write_slot(slot_idx, log_msg);
            buffer.commit_slot(slot_idx);
        }
        
        // 验证写入延迟合理（非阻塞操作应该很快）
        // 计算平均延迟
        uint64_t total_latency_ns = 0;
        for (const auto& lat : latencies) {
            total_latency_ns += lat.count();
        }
        double avg_latency_ns = static_cast<double>(total_latency_ns) / latencies.size();
        
        // 非阻塞写入的平均延迟应该小于1毫秒（1,000,000纳秒）
        // 这是一个宽松的阈值，实际应该更快
        EXPECT_LT(avg_latency_ns, 1000000.0) 
            << "Average write latency should be less than 1ms for non-blocking writes";
        
        // 验证所有消息都已写入
        stats = buffer.get_stats();
        EXPECT_EQ(stats.current_usage, num_writes) 
            << "All messages should be written to the buffer";
    }
}

// **属性19补充：非阻塞写入在缓冲区满时立即返回**
// **验证：需求 11.5**
// **Feature: multiprocess-shared-memory, Property 19**
TEST_F(LockFreeRingBufferTest, Property19_NonBlockingWriteWhenFull) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        size_t slot_size = 512;
        
        // 使用丢弃策略创建缓冲区
        LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
        auto stats = buffer.get_stats();
        size_t capacity = stats.capacity;
        
        // 填满缓冲区
        for (size_t i = 0; i < capacity; ++i) {
            std::string msg_text = "Fill_" + std::to_string(i);
            auto log_msg = create_test_log_msg(msg_text);
            
            auto reserve_result = buffer.reserve_slot();
            if (!reserve_result.is_ok()) {
                break;
            }
            
            size_t slot_idx = reserve_result.value();
            buffer.write_slot(slot_idx, log_msg);
            buffer.commit_slot(slot_idx);
        }
        
        // 验证缓冲区已满
        ASSERT_TRUE(buffer.is_full()) << "Buffer should be full after filling";
        
        // 测试非阻塞写入在缓冲区满时的行为
        auto start = std::chrono::high_resolution_clock::now();
        auto reserve_result = buffer.try_reserve_slot();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        // 验证立即返回错误
        EXPECT_TRUE(reserve_result.is_error()) 
            << "try_reserve_slot should return error when buffer is full";
        
        // 验证非阻塞：操作应该立即返回（小于1毫秒）
        EXPECT_LT(latency.count(), 1000000) 
            << "try_reserve_slot should return immediately when buffer is full";
    }
}

// **属性19补充：reserve_slot在未满时不阻塞**
// **验证：需求 11.5**
// **Feature: multiprocess-shared-memory, Property 19**
TEST_F(LockFreeRingBufferTest, Property19_ReserveSlotNonBlockingWhenNotFull) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 重新初始化共享内存
        std::memset(shared_memory_, 0, buffer_size_);
        
        size_t slot_size = 512;
        
        // 使用阻塞策略创建缓冲区
        // 即使是阻塞策略，在缓冲区未满时也不应该阻塞
        LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Block);
        auto stats = buffer.get_stats();
        size_t capacity = stats.capacity;
        
        // 生成随机数量的写入操作（不超过容量的一半）
        std::uniform_int_distribution<size_t> num_writes_dist(1, capacity / 2);
        size_t num_writes = num_writes_dist(rng);
        
        std::vector<std::chrono::nanoseconds> latencies;
        latencies.reserve(num_writes);
        
        for (size_t i = 0; i < num_writes; ++i) {
            std::string msg_text = "Test_" + std::to_string(i);
            auto log_msg = create_test_log_msg(msg_text);
            
            // 测量reserve_slot延迟
            auto start = std::chrono::high_resolution_clock::now();
            auto reserve_result = buffer.reserve_slot();
            auto end = std::chrono::high_resolution_clock::now();
            
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            latencies.push_back(latency);
            
            // 验证成功
            ASSERT_TRUE(reserve_result.is_ok()) 
                << "reserve_slot should succeed when buffer is not full";
            
            size_t slot_idx = reserve_result.value();
            buffer.write_slot(slot_idx, log_msg);
            buffer.commit_slot(slot_idx);
        }
        
        // 验证所有写入延迟都很短（非阻塞）
        for (size_t i = 0; i < latencies.size(); ++i) {
            EXPECT_LT(latencies[i].count(), 1000000) 
                << "reserve_slot should not block when buffer is not full (iteration " << i << ")";
        }
    }
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
