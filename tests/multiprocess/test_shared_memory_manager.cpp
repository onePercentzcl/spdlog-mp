// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/shared_memory_manager.h>
#include <cstring>

using namespace spdlog;

// 基础测试套件
class SharedMemoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试设置
    }

    void TearDown() override {
        // 测试清理
    }
};

// 测试创建共享内存
TEST_F(SharedMemoryManagerTest, CreateSharedMemory) {
    size_t size = 4096;
    auto result = SharedMemoryManager::create(size);
    
    ASSERT_TRUE(result.is_ok());
    EXPECT_GT(result.value().size, 0u);
    EXPECT_FALSE(result.value().name.empty());
    
    // 清理
    SharedMemoryManager::destroy(result.value());
}

// 测试创建共享内存时使用无效大小
TEST_F(SharedMemoryManagerTest, CreateWithInvalidSize) {
    auto result = SharedMemoryManager::create(0);
    
    ASSERT_TRUE(result.is_error());
    EXPECT_FALSE(result.error_message().empty());
}

// 测试映射共享内存
TEST_F(SharedMemoryManagerTest, AttachSharedMemory) {
    size_t size = 4096;
    auto create_result = SharedMemoryManager::create(size);
    ASSERT_TRUE(create_result.is_ok());
    
    auto attach_result = SharedMemoryManager::attach(create_result.value());
    ASSERT_TRUE(attach_result.is_ok());
    EXPECT_NE(attach_result.value(), nullptr);
    
    // 清理
    SharedMemoryManager::detach(attach_result.value(), size);
    SharedMemoryManager::destroy(create_result.value());
}

// 测试使用无效句柄映射
TEST_F(SharedMemoryManagerTest, AttachWithInvalidHandle) {
    SharedMemoryHandle invalid_handle;
    invalid_handle.fd = -1;
    invalid_handle.size = 0;
    
    auto result = SharedMemoryManager::attach(invalid_handle);
    ASSERT_TRUE(result.is_error());
}

// 测试验证句柄
TEST_F(SharedMemoryManagerTest, ValidateHandle) {
    // 有效句柄
    size_t size = 4096;
    auto result = SharedMemoryManager::create(size);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(SharedMemoryManager::validate(result.value()));
    
    // 无效句柄
    SharedMemoryHandle invalid_handle;
    invalid_handle.fd = -1;
    invalid_handle.size = 0;
    EXPECT_FALSE(SharedMemoryManager::validate(invalid_handle));
    
    // 清理
    SharedMemoryManager::destroy(result.value());
}

// 测试重新初始化
TEST_F(SharedMemoryManagerTest, ReinitializeSharedMemory) {
    size_t initial_size = 4096;
    size_t new_size = 8192;
    
    auto create_result = SharedMemoryManager::create(initial_size);
    ASSERT_TRUE(create_result.is_ok());
    
    auto reinit_result = SharedMemoryManager::reinitialize(create_result.value(), new_size);
    ASSERT_TRUE(reinit_result.is_ok());
    EXPECT_EQ(reinit_result.value().size, new_size);
    
    // 清理
    SharedMemoryManager::destroy(reinit_result.value());
}

// 测试读写共享内存
TEST_F(SharedMemoryManagerTest, ReadWriteSharedMemory) {
    size_t size = 4096;
    auto create_result = SharedMemoryManager::create(size);
    ASSERT_TRUE(create_result.is_ok());
    
    auto attach_result = SharedMemoryManager::attach(create_result.value());
    ASSERT_TRUE(attach_result.is_ok());
    
    // 写入数据
    const char* test_data = "Hello, shared memory!";
    std::memcpy(attach_result.value(), test_data, std::strlen(test_data) + 1);
    
    // 读取数据
    char* read_data = static_cast<char*>(attach_result.value());
    EXPECT_STREQ(read_data, test_data);
    
    // 清理
    SharedMemoryManager::detach(attach_result.value(), size);
    SharedMemoryManager::destroy(create_result.value());
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
