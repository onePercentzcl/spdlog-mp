// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/common.h>
#include <cstdlib>
#include <cstring>

using namespace spdlog;

// 配置功能测试套件
class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 清理可能存在的环境变量
        unsetenv("SPDLOG_SHM_TEST");
    }

    void TearDown() override {
        // 清理环境变量
        unsetenv("SPDLOG_SHM_TEST");
    }
};

// 测试从环境变量读取配置（有效格式：name:size）
TEST_F(ConfigTest, FromEnvValidFormat) {
    setenv("SPDLOG_SHM_TEST", "test_shm:1048576", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 1048576u);
    EXPECT_EQ(result->fd, -1);
}

// 测试从环境变量读取配置（有效格式：name:size:fd）
TEST_F(ConfigTest, FromEnvValidFormatWithFd) {
    setenv("SPDLOG_SHM_TEST", "test_shm:2097152:5", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 2097152u);
    EXPECT_EQ(result->fd, 5);
}

// 测试从环境变量读取配置（环境变量不存在）
TEST_F(ConfigTest, FromEnvNotExist) {
    auto result = config::from_env("NONEXISTENT_VAR");
    
    EXPECT_FALSE(result.has_value());
}

// 测试从环境变量读取配置（空值）
TEST_F(ConfigTest, FromEnvEmpty) {
    setenv("SPDLOG_SHM_TEST", "", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    EXPECT_FALSE(result.has_value());
}

// 测试从环境变量读取配置（无效格式：缺少冒号）
TEST_F(ConfigTest, FromEnvInvalidFormatNoColon) {
    setenv("SPDLOG_SHM_TEST", "test_shm", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    EXPECT_FALSE(result.has_value());
}

// 测试从环境变量读取配置（无效格式：大小为0）
TEST_F(ConfigTest, FromEnvInvalidSizeZero) {
    setenv("SPDLOG_SHM_TEST", "test_shm:0", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    EXPECT_FALSE(result.has_value());
}

// 测试从环境变量读取配置（无效格式：大小不是数字）
TEST_F(ConfigTest, FromEnvInvalidSizeNotNumber) {
    setenv("SPDLOG_SHM_TEST", "test_shm:invalid", 1);
    
    auto result = config::from_env("SPDLOG_SHM_TEST");
    
    EXPECT_FALSE(result.has_value());
}

// 测试从命令行参数读取配置（格式：--shm-name=value）
TEST_F(ConfigTest, FromArgsEqualFormat) {
    const char* argv[] = {"program", "--shm-name=test_shm:1048576"};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 1048576u);
    EXPECT_EQ(result->fd, -1);
}

// 测试从命令行参数读取配置（格式：--shm-name value）
TEST_F(ConfigTest, FromArgsSpaceFormat) {
    const char* argv[] = {"program", "--shm-name", "test_shm:2097152"};
    int argc = 3;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 2097152u);
    EXPECT_EQ(result->fd, -1);
}

// 测试从命令行参数读取配置（带文件描述符）
TEST_F(ConfigTest, FromArgsWithFd) {
    const char* argv[] = {"program", "--shm-name=test_shm:1048576:3"};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 1048576u);
    EXPECT_EQ(result->fd, 3);
}

// 测试从命令行参数读取配置（参数不存在）
TEST_F(ConfigTest, FromArgsNotExist) {
    const char* argv[] = {"program", "--other-arg=value"};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    EXPECT_FALSE(result.has_value());
}

// 测试从命令行参数读取配置（空参数列表）
TEST_F(ConfigTest, FromArgsEmpty) {
    const char* argv[] = {"program"};
    int argc = 1;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    EXPECT_FALSE(result.has_value());
}

// 测试从命令行参数读取配置（无效格式：缺少值）
TEST_F(ConfigTest, FromArgsInvalidNoValue) {
    const char* argv[] = {"program", "--shm-name="};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    EXPECT_FALSE(result.has_value());
}

// 测试从命令行参数读取配置（无效格式：大小为0）
TEST_F(ConfigTest, FromArgsInvalidSizeZero) {
    const char* argv[] = {"program", "--shm-name=test_shm:0"};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv));
    
    EXPECT_FALSE(result.has_value());
}

// 测试从命令行参数读取配置（自定义参数名）
TEST_F(ConfigTest, FromArgsCustomArgName) {
    const char* argv[] = {"program", "--custom-shm=test_shm:1048576"};
    int argc = 2;
    
    auto result = config::from_args(argc, const_cast<char**>(argv), "--custom-shm");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "test_shm");
    EXPECT_EQ(result->size, 1048576u);
}

// 测试BufferStats结构的默认值
TEST_F(ConfigTest, BufferStatsDefaultValues) {
    BufferStats stats;
    
    EXPECT_EQ(stats.total_writes, 0u);
    EXPECT_EQ(stats.total_reads, 0u);
    EXPECT_EQ(stats.dropped_messages, 0u);
    EXPECT_EQ(stats.current_usage, 0u);
    EXPECT_EQ(stats.capacity, 0u);
}

// 测试BufferStats结构的赋值
TEST_F(ConfigTest, BufferStatsAssignment) {
    BufferStats stats;
    stats.total_writes = 100;
    stats.total_reads = 80;
    stats.dropped_messages = 5;
    stats.current_usage = 20;
    stats.capacity = 256;
    
    EXPECT_EQ(stats.total_writes, 100u);
    EXPECT_EQ(stats.total_reads, 80u);
    EXPECT_EQ(stats.dropped_messages, 5u);
    EXPECT_EQ(stats.current_usage, 20u);
    EXPECT_EQ(stats.capacity, 256u);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
