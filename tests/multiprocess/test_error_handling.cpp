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
#include <cstring>
#include <chrono>
#include <thread>

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

// 辅助函数：计算元数据大小（对齐到64字节）
size_t get_metadata_size() {
    // 这个值需要与LockFreeRingBuffer中的计算一致
    // Metadata结构体大小对齐到64字节
    size_t metadata_size = 256;  // 保守估计，包含多个alignas(64)的原子变量
    return (metadata_size + 63) & ~63;
}

// 辅助函数：获取槽位指针
LockFreeRingBuffer::Slot* get_slot_ptr(void* memory, size_t slot_index, size_t slot_size) {
    size_t metadata_size = get_metadata_size();
    return reinterpret_cast<LockFreeRingBuffer::Slot*>(
        static_cast<char*>(memory) + metadata_size + slot_index * slot_size);
}
} // anonymous namespace

// 错误处理测试套件
class ErrorHandlingTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_size_ = 1024 * 1024; // 1MB
    }

    void TearDown() override {
        // 清理
    }

    size_t buffer_size_ = 0;
};

// **属性15：崩溃恢复 - 检测并跳过陈旧的未提交槽位**
// **验证：需求 7.2**
// **Feature: multiprocess-shared-memory, Property 15**
TEST_F(ErrorHandlingTest, Property15_CrashRecovery_SkipStaleSlots) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 分配本地内存模拟共享内存
        std::vector<char> memory(buffer_size_, 0);
        void* shm_ptr = memory.data();
        
        size_t slot_size = 512;
        
        // 创建并初始化环形缓冲区
        LockFreeRingBuffer buffer(shm_ptr, buffer_size_, slot_size, OverflowPolicy::Drop, true);
        
        // 模拟部分写入的消息（生产者崩溃）
        // 预留槽位，写入数据但不提交，并设置陈旧时间戳
        std::uniform_int_distribution<size_t> num_stale_dist(1, 5);
        size_t num_stale_slots = num_stale_dist(rng);
        
        for (size_t i = 0; i < num_stale_slots; ++i) {
            auto reserve_result = buffer.reserve_slot();
            ASSERT_TRUE(reserve_result.is_ok()) << "迭代 " << iter << ": 预留槽位失败";
            
            size_t slot_idx = reserve_result.value();
            
            // 创建一个10秒前的时间点
            auto old_time = log_clock::now() - std::chrono::seconds(10);
            
            // 写入部分数据但不提交
            auto log_msg = details::log_msg(
                old_time,  // 使用10秒前的时间
                source_loc{},
                string_view_t("test_logger"),
                level::info,
                string_view_t("Stale message")
            );
            buffer.write_slot(slot_idx, log_msg);
            // 不调用commit_slot，模拟崩溃
        }
        
        // 验证缓冲区不为空（有未提交的槽位）
        auto stats = buffer.get_stats();
        EXPECT_EQ(stats.current_usage, num_stale_slots) 
            << "迭代 " << iter << ": 当前使用量应该等于陈旧槽位数";
        
        // 检测陈旧槽位
        bool has_stale = buffer.is_next_slot_stale(5);
        EXPECT_TRUE(has_stale) << "迭代 " << iter << ": 应该检测到陈旧槽位";
        
        // 跳过陈旧槽位
        size_t skipped = buffer.skip_stale_slots(5);
        
        // 验证跳过的数量
        EXPECT_EQ(skipped, num_stale_slots) 
            << "迭代 " << iter << ": 跳过的陈旧槽位数量不匹配";
        
        // 验证没有更多陈旧槽位
        EXPECT_FALSE(buffer.is_next_slot_stale(5));
        
        // 验证缓冲区现在为空
        stats = buffer.get_stats();
        EXPECT_EQ(stats.current_usage, 0u) 
            << "迭代 " << iter << ": 跳过后缓冲区应该为空";
    }
}

// **属性16：版本兼容性检测**
// **验证：需求 7.5**
// **Feature: multiprocess-shared-memory, Property 16**
TEST_F(ErrorHandlingTest, Property16_VersionCompatibility) {
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 rng(rd());
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 使用本地内存模拟共享内存，避免系统共享内存名称冲突
        std::vector<char> memory(buffer_size_, 0);
        void* shm_ptr = memory.data();
        
        size_t slot_size = 512;
        
        // 初始化环形缓冲区（设置正确的版本号）
        LockFreeRingBuffer buffer(shm_ptr, buffer_size_, slot_size, OverflowPolicy::Drop, true);
        
        // 验证版本号正确
        uint32_t* version_ptr = static_cast<uint32_t*>(shm_ptr);
        EXPECT_EQ(*version_ptr, MULTIPROCESS_VERSION) 
            << "迭代 " << iter << ": 版本号应该正确初始化";
        
        // 创建实际的共享内存来测试attach_with_version_check
        std::string shm_name = "/test_ver_" + std::to_string(iter) + "_" + 
                               std::to_string(rd());
        auto create_result = SharedMemoryManager::create(buffer_size_, shm_name);
        if (create_result.is_error()) {
            // 如果创建失败（可能是名称冲突），跳过这次迭代
            continue;
        }
        auto handle = create_result.value();
        
        // 映射并初始化
        auto attach_result = SharedMemoryManager::attach(handle);
        ASSERT_TRUE(attach_result.is_ok()) << "迭代 " << iter << ": attach失败";
        void* real_shm_ptr = attach_result.value();
        
        // 初始化环形缓冲区
        LockFreeRingBuffer real_buffer(real_shm_ptr, handle.size, slot_size, OverflowPolicy::Drop, true);
        
        // 验证正确版本可以映射
        auto attach_with_version = SharedMemoryManager::attach_with_version_check(handle);
        EXPECT_TRUE(attach_with_version.is_ok()) 
            << "迭代 " << iter << ": 正确版本应该能成功映射";
        
        if (attach_with_version.is_ok()) {
            SharedMemoryManager::detach(attach_with_version.value(), handle.size);
        }
        
        // 修改版本号为不兼容的版本
        std::uniform_int_distribution<uint32_t> version_dist(100, 1000);
        uint32_t wrong_version = version_dist(rng);
        
        // 直接修改共享内存中的版本号
        uint32_t* real_version_ptr = static_cast<uint32_t*>(real_shm_ptr);
        *real_version_ptr = wrong_version;
        
        // 验证错误版本无法映射
        auto attach_wrong_version = SharedMemoryManager::attach_with_version_check(handle);
        EXPECT_TRUE(attach_wrong_version.is_error()) 
            << "迭代 " << iter << ": 错误版本应该返回错误";
        
        if (attach_wrong_version.is_error()) {
            // 验证错误消息包含版本信息
            std::string error_msg = attach_wrong_version.error_message();
            EXPECT_NE(error_msg.find("Version mismatch"), std::string::npos)
                << "错误消息应该包含'Version mismatch'";
        }
        
        // 清理
        SharedMemoryManager::detach(real_shm_ptr, handle.size);
        SharedMemoryManager::destroy(handle);
    }
}

// 测试版本检查 - 单元测试
TEST_F(ErrorHandlingTest, VersionCheckBasic) {
    // 创建共享内存
    auto create_result = SharedMemoryManager::create(buffer_size_, "/test_version_basic");
    ASSERT_TRUE(create_result.is_ok());
    auto handle = create_result.value();
    
    // 映射并初始化
    auto attach_result = SharedMemoryManager::attach(handle);
    ASSERT_TRUE(attach_result.is_ok());
    void* shm_ptr = attach_result.value();
    
    // 初始化环形缓冲区
    LockFreeRingBuffer buffer(shm_ptr, handle.size, 512, OverflowPolicy::Drop, true);
    
    // 验证版本号正确
    uint32_t* version_ptr = static_cast<uint32_t*>(shm_ptr);
    EXPECT_EQ(*version_ptr, MULTIPROCESS_VERSION);
    
    // 使用版本检查映射
    auto attach_with_check = SharedMemoryManager::attach_with_version_check(handle);
    EXPECT_TRUE(attach_with_check.is_ok());
    
    if (attach_with_check.is_ok()) {
        SharedMemoryManager::detach(attach_with_check.value(), handle.size);
    }
    
    // 清理
    SharedMemoryManager::detach(shm_ptr, handle.size);
    SharedMemoryManager::destroy(handle);
}

// 测试崩溃恢复 - 单元测试
TEST_F(ErrorHandlingTest, CrashRecoveryBasic) {
    // 分配本地内存
    std::vector<char> memory(buffer_size_, 0);
    void* shm_ptr = memory.data();
    
    size_t slot_size = 512;
    
    // 创建环形缓冲区
    LockFreeRingBuffer buffer(shm_ptr, buffer_size_, slot_size, OverflowPolicy::Drop, true);
    
    // 写入一条正常消息
    auto log_msg = details::log_msg(
        log_clock::now(),
        source_loc{},
        string_view_t("test_logger"),
        level::info,
        string_view_t("Normal message")
    );
    
    auto reserve_result = buffer.reserve_slot();
    ASSERT_TRUE(reserve_result.is_ok());
    buffer.write_slot(reserve_result.value(), log_msg);
    buffer.commit_slot(reserve_result.value());
    
    // 读取正常消息
    EXPECT_TRUE(buffer.is_next_slot_committed());
    std::vector<char> read_buffer(slot_size);
    auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
    ASSERT_TRUE(read_result.is_ok());
    buffer.release_slot();
    
    // 模拟部分写入（预留但不提交）
    auto stale_reserve = buffer.reserve_slot();
    ASSERT_TRUE(stale_reserve.is_ok());
    size_t stale_idx = stale_reserve.value();
    
    // 创建一个10秒前的时间点
    auto old_time = log_clock::now() - std::chrono::seconds(10);
    
    // 写入部分数据但不提交
    auto stale_msg = details::log_msg(
        old_time,
        source_loc{},
        string_view_t("test_logger"),
        level::info,
        string_view_t("Stale message")
    );
    buffer.write_slot(stale_idx, stale_msg);
    // 不调用commit_slot，模拟崩溃
    
    // 检测陈旧槽位
    EXPECT_FALSE(buffer.is_next_slot_committed());
    EXPECT_TRUE(buffer.is_next_slot_stale(5));
    
    // 跳过陈旧槽位
    size_t skipped = buffer.skip_stale_slots(5);
    EXPECT_EQ(skipped, 1u);
    
    // 验证没有更多数据
    EXPECT_FALSE(buffer.is_next_slot_committed());
    EXPECT_FALSE(buffer.is_next_slot_stale(5));
}

// 测试错误回退机制 - 单元测试
TEST_F(ErrorHandlingTest, FallbackMechanismBasic) {
    // 创建一个无效的共享内存句柄
    SharedMemoryHandle invalid_handle;
    invalid_handle.fd = -1;
    invalid_handle.name = "/nonexistent_shm";
    invalid_handle.size = buffer_size_;
    
    // 创建回退sink
    std::ostringstream oss;
    auto fallback_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    fallback_sink->set_pattern("%v");
    
    // 配置启用回退
    ProducerConfig config;
    config.enable_fallback = true;
    config.fallback_sink = fallback_sink;
    
    // 创建生产者sink（应该使用回退模式）
    auto producer_sink = std::make_shared<shared_memory_producer_sink_mt>(invalid_handle, config);
    
    // 验证正在使用回退模式
    EXPECT_TRUE(producer_sink->is_using_fallback());
    EXPECT_FALSE(producer_sink->is_shared_memory_available());
    
    // 创建logger并写入日志
    auto logger = std::make_shared<spdlog::logger>("fallback_test", producer_sink);
    logger->info("Test message via fallback");
    logger->flush();
    
    // 验证消息被写入回退sink
    std::string output = oss.str();
    EXPECT_NE(output.find("Test message via fallback"), std::string::npos);
}

// 测试无回退时的错误处理
TEST_F(ErrorHandlingTest, NoFallbackThrowsException) {
    // 创建一个无效的共享内存句柄
    SharedMemoryHandle invalid_handle;
    invalid_handle.fd = -1;
    invalid_handle.name = "/nonexistent_shm";
    invalid_handle.size = buffer_size_;
    
    // 配置不启用回退
    ProducerConfig config;
    config.enable_fallback = false;
    
    // 创建生产者sink应该抛出异常
    EXPECT_THROW(
        std::make_shared<shared_memory_producer_sink_mt>(invalid_handle, config),
        spdlog_ex
    );
}

// 测试陈旧槽位阈值
TEST_F(ErrorHandlingTest, StaleSlotThreshold) {
    std::vector<char> memory(buffer_size_, 0);
    void* shm_ptr = memory.data();
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shm_ptr, buffer_size_, slot_size, OverflowPolicy::Drop, true);
    
    // 预留一个槽位并写入3秒前的时间戳
    auto reserve_result = buffer.reserve_slot();
    ASSERT_TRUE(reserve_result.is_ok());
    size_t slot_idx = reserve_result.value();
    
    // 创建一个3秒前的时间点
    auto time_3s_ago = log_clock::now() - std::chrono::seconds(3);
    
    // 写入部分数据但不提交
    auto log_msg = details::log_msg(
        time_3s_ago,
        source_loc{},
        string_view_t("test_logger"),
        level::info,
        string_view_t("Test message")
    );
    buffer.write_slot(slot_idx, log_msg);
    // 不调用commit_slot
    
    // 使用5秒阈值，不应该被认为是陈旧的
    EXPECT_FALSE(buffer.is_next_slot_stale(5));
    
    // 使用2秒阈值，应该被认为是陈旧的
    EXPECT_TRUE(buffer.is_next_slot_stale(2));
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
