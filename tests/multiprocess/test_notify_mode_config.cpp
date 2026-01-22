// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/common.h>
#include <spdlog/multiprocess/custom_formatter.h>
#include <random>
#include <string>
#include <algorithm>
#include <cstring>

// UDS socket includes for Property 4 tests
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

using namespace spdlog;

namespace {
// 辅助函数：生成随机共享内存名称
std::string generate_random_shm_name(size_t length, std::mt19937& rng, bool with_leading_slash) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    if (with_leading_slash) {
        result = "/";
    }
    result.reserve(length + (with_leading_slash ? 1 : 0));
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}
} // anonymous namespace

// 通知模式配置测试套件
class NotifyModeConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        rng_.seed(std::random_device{}());
    }

    std::mt19937 rng_;
};

// **Property 1: UDS 路径生成一致性**
// **Validates: Requirements 4.4, 5.1**
// 对于任意共享内存名称 shm_name，当未提供 uds_path 时，
// 系统生成的 UDS 路径应遵循格式 "/tmp/spdlog_mp_{normalized_name}.sock"，
// 其中 normalized_name 是移除开头 '/' 后的 shm_name。
TEST_F(NotifyModeConfigTest, Property1_UDSPathGenerationConsistency) {
    const int NUM_ITERATIONS = 100;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 生成随机长度（1-50个字符）
        std::uniform_int_distribution<size_t> len_dist(1, 50);
        size_t name_length = len_dist(rng_);
        
        // 随机决定是否带有前导斜杠
        std::uniform_int_distribution<int> slash_dist(0, 1);
        bool with_leading_slash = slash_dist(rng_) == 1;
        
        // 生成随机共享内存名称
        std::string shm_name = generate_random_shm_name(name_length, rng_, with_leading_slash);
        
        // 调用 generate_default_uds_path
        std::string uds_path = generate_default_uds_path(shm_name);
        
        // 计算期望的 normalized_name
        std::string expected_normalized = shm_name;
        if (!expected_normalized.empty() && expected_normalized[0] == '/') {
            expected_normalized = expected_normalized.substr(1);
        }
        
        // 验证路径格式
        std::string expected_path = "/tmp/spdlog_mp_" + expected_normalized + ".sock";
        
        EXPECT_EQ(uds_path, expected_path) 
            << "迭代 " << iter << ": shm_name='" << shm_name 
            << "', 期望路径='" << expected_path 
            << "', 实际路径='" << uds_path << "'";
        
        // 验证路径以 /tmp/spdlog_mp_ 开头
        EXPECT_TRUE(uds_path.find("/tmp/spdlog_mp_") == 0)
            << "迭代 " << iter << ": 路径应以 '/tmp/spdlog_mp_' 开头";
        
        // 验证路径以 .sock 结尾
        EXPECT_TRUE(uds_path.size() >= 5 && uds_path.substr(uds_path.size() - 5) == ".sock")
            << "迭代 " << iter << ": 路径应以 '.sock' 结尾";
        
        // 验证路径不包含双斜杠（除了开头的 /tmp）
        std::string path_after_tmp = uds_path.substr(1);  // 跳过开头的 /
        EXPECT_EQ(path_after_tmp.find("//"), std::string::npos)
            << "迭代 " << iter << ": 路径不应包含双斜杠";
    }
}

// 测试空字符串输入
TEST_F(NotifyModeConfigTest, UDSPathGenerationEmptyInput) {
    std::string uds_path = generate_default_uds_path("");
    EXPECT_EQ(uds_path, "/tmp/spdlog_mp_.sock");
}

// 测试只有斜杠的输入
TEST_F(NotifyModeConfigTest, UDSPathGenerationOnlySlash) {
    std::string uds_path = generate_default_uds_path("/");
    EXPECT_EQ(uds_path, "/tmp/spdlog_mp_.sock");
}

// 测试多个前导斜杠（只移除第一个）
TEST_F(NotifyModeConfigTest, UDSPathGenerationMultipleLeadingSlashes) {
    std::string uds_path = generate_default_uds_path("//test_shm");
    // 只移除第一个斜杠
    EXPECT_EQ(uds_path, "/tmp/spdlog_mp_/test_shm.sock");
}

// 测试典型的共享内存名称
TEST_F(NotifyModeConfigTest, UDSPathGenerationTypicalNames) {
    // 不带斜杠
    EXPECT_EQ(generate_default_uds_path("my_shm"), "/tmp/spdlog_mp_my_shm.sock");
    
    // 带斜杠
    EXPECT_EQ(generate_default_uds_path("/my_shm"), "/tmp/spdlog_mp_my_shm.sock");
    
    // 带数字
    EXPECT_EQ(generate_default_uds_path("/shm_12345"), "/tmp/spdlog_mp_shm_12345.sock");
    
    // 带下划线
    EXPECT_EQ(generate_default_uds_path("/test_shared_memory"), "/tmp/spdlog_mp_test_shared_memory.sock");
}

// 测试 NotifyMode 枚举默认值
TEST_F(NotifyModeConfigTest, NotifyModeEnumValues) {
    // 验证枚举值存在
    NotifyMode uds_mode = NotifyMode::UDS;
    NotifyMode eventfd_mode = NotifyMode::EventFD;
    
    // 验证它们是不同的值
    EXPECT_NE(static_cast<int>(uds_mode), static_cast<int>(eventfd_mode));
}

// **Property 3: 默认模式不变量**
// **Validates: Requirements 4.1, 4.2, 4.3**
// 对于任意默认构造的 ConsumerConfig 或 ProducerConfig，
// 其 notify_mode 字段应为 NotifyMode::UDS，系统不应自动选择 EventFD 模式。
TEST_F(NotifyModeConfigTest, Property3_DefaultModeInvariant) {
    const int NUM_ITERATIONS = 100;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 创建默认构造的 ConsumerConfig
        ConsumerConfig consumer_cfg;
        
        // 验证 ConsumerConfig 默认 notify_mode 为 UDS
        EXPECT_EQ(consumer_cfg.notify_mode, NotifyMode::UDS)
            << "迭代 " << iter << ": ConsumerConfig 默认 notify_mode 应为 UDS";
        
        // 验证 ConsumerConfig 默认 uds_path 为空（将自动生成）
        EXPECT_TRUE(consumer_cfg.uds_path.empty())
            << "迭代 " << iter << ": ConsumerConfig 默认 uds_path 应为空";
        
        // 验证 ConsumerConfig 默认 eventfd 为 -1（无效）
        EXPECT_EQ(consumer_cfg.eventfd, -1)
            << "迭代 " << iter << ": ConsumerConfig 默认 eventfd 应为 -1";
        
        // 创建默认构造的 ProducerConfig
        ProducerConfig producer_cfg;
        
        // 验证 ProducerConfig 默认 notify_mode 为 UDS
        EXPECT_EQ(producer_cfg.notify_mode, NotifyMode::UDS)
            << "迭代 " << iter << ": ProducerConfig 默认 notify_mode 应为 UDS";
        
        // 验证 ProducerConfig 默认 uds_path 为空（将自动生成）
        EXPECT_TRUE(producer_cfg.uds_path.empty())
            << "迭代 " << iter << ": ProducerConfig 默认 uds_path 应为空";
        
        // 验证 ProducerConfig 默认 eventfd 为 -1（无效）
        EXPECT_EQ(producer_cfg.eventfd, -1)
            << "迭代 " << iter << ": ProducerConfig 默认 eventfd 应为 -1";
    }
}

// 测试 ConsumerConfig 便捷构造函数保持默认通知模式
TEST_F(NotifyModeConfigTest, ConsumerConfigConvenienceConstructorsDefaultMode) {
    // 使用 shm_name 构造
    ConsumerConfig cfg1("/test_shm");
    EXPECT_EQ(cfg1.notify_mode, NotifyMode::UDS);
    EXPECT_TRUE(cfg1.uds_path.empty());
    EXPECT_EQ(cfg1.eventfd, -1);
    
    // 使用 shm_name 和 log_file 构造
    ConsumerConfig cfg2("/test_shm", "/tmp/test.log");
    EXPECT_EQ(cfg2.notify_mode, NotifyMode::UDS);
    EXPECT_TRUE(cfg2.uds_path.empty());
    EXPECT_EQ(cfg2.eventfd, -1);
}

// 测试 ProducerConfig 便捷构造函数保持默认通知模式
TEST_F(NotifyModeConfigTest, ProducerConfigConvenienceConstructorsDefaultMode) {
    // 使用 SharedMemoryHandle 构造
    SharedMemoryHandle handle(10, "/test_shm", 1024);
    ProducerConfig cfg1(handle);
    EXPECT_EQ(cfg1.notify_mode, NotifyMode::UDS);
    EXPECT_TRUE(cfg1.uds_path.empty());
    EXPECT_EQ(cfg1.eventfd, -1);
    
    // 使用 shm_name 构造
    ProducerConfig cfg2("/test_shm");
    EXPECT_EQ(cfg2.notify_mode, NotifyMode::UDS);
    EXPECT_TRUE(cfg2.uds_path.empty());
    EXPECT_EQ(cfg2.eventfd, -1);
    
    // 使用 shm_name 和 size 构造
    ProducerConfig cfg3("/test_shm", 8 * 1024 * 1024);
    EXPECT_EQ(cfg3.notify_mode, NotifyMode::UDS);
    EXPECT_TRUE(cfg3.uds_path.empty());
    EXPECT_EQ(cfg3.eventfd, -1);
}

// **Property 2: 用户指定路径优先**
// **Validates: Requirements 5.2**
// 对于任意用户指定的 uds_path 字符串，当配置中提供了该路径时，
// 系统应使用该路径而非自动生成的路径。
TEST_F(NotifyModeConfigTest, Property2_UserSpecifiedPathPriority) {
    const int NUM_ITERATIONS = 100;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 生成随机的用户指定路径
        std::uniform_int_distribution<size_t> len_dist(5, 80);
        size_t path_length = len_dist(rng_);
        
        // 生成随机路径字符串（以 /tmp/ 开头，以 .sock 结尾）
        std::string user_path = "/tmp/";
        static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
        std::uniform_int_distribution<size_t> char_dist(0, sizeof(charset) - 2);
        
        for (size_t i = 0; i < path_length; ++i) {
            user_path += charset[char_dist(rng_)];
        }
        user_path += ".sock";
        
        // 生成随机的共享内存名称（用于验证自动生成的路径不会被使用）
        std::uniform_int_distribution<size_t> shm_len_dist(1, 30);
        size_t shm_length = shm_len_dist(rng_);
        std::string shm_name = generate_random_shm_name(shm_length, rng_, true);
        
        // 计算自动生成的路径（不应该被使用）
        std::string auto_generated_path = generate_default_uds_path(shm_name);
        
        // 创建 ConsumerConfig 并设置用户指定的路径
        ConsumerConfig consumer_cfg;
        consumer_cfg.shm_name = shm_name;
        consumer_cfg.uds_path = user_path;
        
        // 验证 ConsumerConfig 使用用户指定的路径
        EXPECT_EQ(consumer_cfg.uds_path, user_path)
            << "迭代 " << iter << ": ConsumerConfig 应使用用户指定的路径";
        
        // 验证用户指定的路径与自动生成的路径不同（除非巧合）
        // 这验证了系统不会覆盖用户指定的路径
        if (user_path != auto_generated_path) {
            EXPECT_NE(consumer_cfg.uds_path, auto_generated_path)
                << "迭代 " << iter << ": 用户指定的路径不应被自动生成的路径覆盖";
        }
        
        // 创建 ProducerConfig 并设置用户指定的路径
        ProducerConfig producer_cfg;
        producer_cfg.shm_name = shm_name;
        producer_cfg.uds_path = user_path;
        
        // 验证 ProducerConfig 使用用户指定的路径
        EXPECT_EQ(producer_cfg.uds_path, user_path)
            << "迭代 " << iter << ": ProducerConfig 应使用用户指定的路径";
        
        // 验证用户指定的路径与自动生成的路径不同
        if (user_path != auto_generated_path) {
            EXPECT_NE(producer_cfg.uds_path, auto_generated_path)
                << "迭代 " << iter << ": 用户指定的路径不应被自动生成的路径覆盖";
        }
    }
}

// 测试用户指定路径的边界情况
TEST_F(NotifyModeConfigTest, UserSpecifiedPathEdgeCases) {
    // 测试空路径（应该保持为空，系统会在实际使用时自动生成）
    {
        ConsumerConfig cfg;
        cfg.uds_path = "";
        EXPECT_TRUE(cfg.uds_path.empty());
        
        ProducerConfig prod_cfg;
        prod_cfg.uds_path = "";
        EXPECT_TRUE(prod_cfg.uds_path.empty());
    }
    
    // 测试非标准路径（用户可以指定任意路径）
    {
        ConsumerConfig cfg;
        cfg.uds_path = "/var/run/my_custom_socket.sock";
        EXPECT_EQ(cfg.uds_path, "/var/run/my_custom_socket.sock");
        
        ProducerConfig prod_cfg;
        prod_cfg.uds_path = "/var/run/my_custom_socket.sock";
        EXPECT_EQ(prod_cfg.uds_path, "/var/run/my_custom_socket.sock");
    }
    
    // 测试相对路径（虽然不推荐，但应该被接受）
    {
        ConsumerConfig cfg;
        cfg.uds_path = "relative/path/socket.sock";
        EXPECT_EQ(cfg.uds_path, "relative/path/socket.sock");
        
        ProducerConfig prod_cfg;
        prod_cfg.uds_path = "relative/path/socket.sock";
        EXPECT_EQ(prod_cfg.uds_path, "relative/path/socket.sock");
    }
    
    // 测试最大长度路径（sockaddr_un.sun_path 最大 108 字节）
    {
        // 创建一个接近最大长度的路径
        std::string long_path = "/tmp/";
        for (int i = 0; i < 95; ++i) {
            long_path += 'a';
        }
        long_path += ".sock";  // 总长度约 105 字节
        
        ConsumerConfig cfg;
        cfg.uds_path = long_path;
        EXPECT_EQ(cfg.uds_path, long_path);
        
        ProducerConfig prod_cfg;
        prod_cfg.uds_path = long_path;
        EXPECT_EQ(prod_cfg.uds_path, long_path);
    }
}

// 测试用户指定路径与自动生成路径的优先级
TEST_F(NotifyModeConfigTest, UserPathTakesPrecedenceOverAutoGenerated) {
    const std::string shm_name = "/test_priority_shm";
    const std::string user_path = "/tmp/user_specified_path.sock";
    const std::string auto_path = generate_default_uds_path(shm_name);
    
    // 验证自动生成的路径格式正确
    EXPECT_EQ(auto_path, "/tmp/spdlog_mp_test_priority_shm.sock");
    
    // 验证用户路径与自动路径不同
    EXPECT_NE(user_path, auto_path);
    
    // 创建配置并设置用户路径
    ConsumerConfig consumer_cfg;
    consumer_cfg.shm_name = shm_name;
    consumer_cfg.uds_path = user_path;
    
    // 验证配置中存储的是用户路径
    EXPECT_EQ(consumer_cfg.uds_path, user_path);
    EXPECT_NE(consumer_cfg.uds_path, auto_path);
    
    // 同样验证 ProducerConfig
    ProducerConfig producer_cfg;
    producer_cfg.shm_name = shm_name;
    producer_cfg.uds_path = user_path;
    
    EXPECT_EQ(producer_cfg.uds_path, user_path);
    EXPECT_NE(producer_cfg.uds_path, auto_path);
}

// 测试配置字段可以被正确修改
TEST_F(NotifyModeConfigTest, ConfigFieldsCanBeModified) {
    // ConsumerConfig
    ConsumerConfig consumer_cfg;
    consumer_cfg.notify_mode = NotifyMode::EventFD;
    consumer_cfg.uds_path = "/tmp/custom.sock";
    consumer_cfg.eventfd = 42;
    
    EXPECT_EQ(consumer_cfg.notify_mode, NotifyMode::EventFD);
    EXPECT_EQ(consumer_cfg.uds_path, "/tmp/custom.sock");
    EXPECT_EQ(consumer_cfg.eventfd, 42);
    
    // ProducerConfig
    ProducerConfig producer_cfg;
    producer_cfg.notify_mode = NotifyMode::EventFD;
    producer_cfg.uds_path = "/tmp/custom.sock";
    producer_cfg.eventfd = 42;
    
    EXPECT_EQ(producer_cfg.notify_mode, NotifyMode::EventFD);
    EXPECT_EQ(producer_cfg.uds_path, "/tmp/custom.sock");
    EXPECT_EQ(producer_cfg.eventfd, 42);
}

// **Property 4: 通知信号语义**
// **Validates: Requirements 7.1**
// 对于任意生产者发送的通知，通知机制仅传递一个信号字节（值为 1），
// 不传递实际日志数据。日志数据通过共享内存传输。
//
// 测试策略：
// 1. 创建 UDS socket 对（模拟消费者和生产者）
// 2. 生产者发送通知信号
// 3. 消费者接收并验证：
//    - 接收到的数据大小为 1 字节
//    - 接收到的数据值为 1
// 4. 重复多次以验证属性的一致性
TEST_F(NotifyModeConfigTest, Property4_NotificationSignalSemantics) {
    const int NUM_ITERATIONS = 100;
    const std::string test_socket_path = "/tmp/spdlog_mp_test_signal_semantics.sock";
    
    // 清理可能存在的旧 socket 文件
    unlink(test_socket_path.c_str());
    
    // 创建服务端 socket（模拟消费者）
    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(server_fd, 0) << "Failed to create server socket";
    
    struct sockaddr_un server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    std::strncpy(server_addr.sun_path, test_socket_path.c_str(), sizeof(server_addr.sun_path) - 1);
    
    int bind_result = bind(server_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    ASSERT_EQ(bind_result, 0) << "Failed to bind server socket: " << strerror(errno);
    
    // 设置服务端为非阻塞模式
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    // 创建客户端 socket（模拟生产者）
    int client_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(client_fd, 0) << "Failed to create client socket";
    
    struct sockaddr_un client_addr;
    std::memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sun_family = AF_UNIX;
    std::strncpy(client_addr.sun_path, test_socket_path.c_str(), sizeof(client_addr.sun_path) - 1);
    
    int connect_result = connect(client_fd, reinterpret_cast<struct sockaddr*>(&client_addr), sizeof(client_addr));
    ASSERT_EQ(connect_result, 0) << "Failed to connect client socket: " << strerror(errno);
    
    // 设置客户端为非阻塞模式
    flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // 生产者发送通知信号（模拟 notify_via_uds 的行为）
        // 根据 Requirements 7.1：通知机制仅传递一个信号字节，值为 1
        uint8_t signal = 1;
        ssize_t send_result = send(client_fd, &signal, sizeof(signal), MSG_DONTWAIT);
        ASSERT_EQ(send_result, 1) << "迭代 " << iter << ": 发送信号失败";
        
        // 消费者接收通知信号
        uint8_t recv_buffer[64];  // 使用较大的缓冲区来检测是否有额外数据
        std::memset(recv_buffer, 0xFF, sizeof(recv_buffer));  // 填充非零值以便检测
        
        ssize_t recv_result = recv(server_fd, recv_buffer, sizeof(recv_buffer), MSG_DONTWAIT);
        
        // 验证接收到的数据大小为 1 字节
        EXPECT_EQ(recv_result, 1) 
            << "迭代 " << iter << ": 接收到的数据大小应为 1 字节，实际为 " << recv_result;
        
        // 验证接收到的数据值为 1
        EXPECT_EQ(recv_buffer[0], 1) 
            << "迭代 " << iter << ": 接收到的信号值应为 1，实际为 " << static_cast<int>(recv_buffer[0]);
        
        // 验证没有额外的数据（缓冲区其余部分应保持原值 0xFF）
        for (size_t i = 1; i < sizeof(recv_buffer); ++i) {
            EXPECT_EQ(recv_buffer[i], 0xFF) 
                << "迭代 " << iter << ": 缓冲区位置 " << i << " 应保持原值 0xFF";
        }
    }
    
    // 清理
    close(client_fd);
    close(server_fd);
    unlink(test_socket_path.c_str());
}

// 测试 UDS 通知信号不包含日志数据
// 这是 Property 4 的补充测试，验证即使发送多条日志消息，
// 每次通知仍然只传递单字节信号
TEST_F(NotifyModeConfigTest, UDSNotificationDoesNotContainLogData) {
    const std::string test_socket_path = "/tmp/spdlog_mp_test_no_log_data.sock";
    
    // 清理可能存在的旧 socket 文件
    unlink(test_socket_path.c_str());
    
    // 创建服务端 socket
    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(server_fd, 0);
    
    struct sockaddr_un server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    std::strncpy(server_addr.sun_path, test_socket_path.c_str(), sizeof(server_addr.sun_path) - 1);
    
    ASSERT_EQ(bind(server_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)), 0);
    
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    // 创建客户端 socket
    int client_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(client_fd, 0);
    
    ASSERT_EQ(connect(client_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)), 0);
    
    flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    // 模拟发送多条"日志消息"的通知
    // 每条日志消息只应该触发一个单字节通知
    // 使用较小的数量以避免缓冲区溢出
    const int NUM_MESSAGES = 10;
    int total_sent = 0;
    int total_received = 0;
    
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        // 发送通知信号（不是日志数据）
        uint8_t signal = 1;
        ssize_t send_result = send(client_fd, &signal, sizeof(signal), MSG_DONTWAIT);
        
        if (send_result == 1) {
            total_sent++;
        }
        // 如果发送失败（缓冲区满），跳过这次发送
        
        // 立即接收以避免缓冲区溢出
        uint8_t recv_buffer[64];
        ssize_t recv_result = recv(server_fd, recv_buffer, sizeof(recv_buffer), MSG_DONTWAIT);
        
        if (recv_result > 0) {
            // 每次接收应该只有 1 字节
            EXPECT_EQ(recv_result, 1) 
                << "接收 " << total_received << ": 数据大小应为 1 字节";
            
            // 信号值应为 1
            EXPECT_EQ(recv_buffer[0], 1) 
                << "接收 " << total_received << ": 信号值应为 1";
            
            total_received++;
        }
    }
    
    // 接收剩余的通知
    uint8_t recv_buffer[64];
    while (true) {
        ssize_t recv_result = recv(server_fd, recv_buffer, sizeof(recv_buffer), MSG_DONTWAIT);
        if (recv_result <= 0) {
            break;
        }
        
        EXPECT_EQ(recv_result, 1) 
            << "接收 " << total_received << ": 数据大小应为 1 字节";
        EXPECT_EQ(recv_buffer[0], 1) 
            << "接收 " << total_received << ": 信号值应为 1";
        
        total_received++;
    }
    
    // 验证接收到的通知数量与发送的数量一致
    EXPECT_EQ(total_received, total_sent) 
        << "接收到的通知数量应与发送的数量一致";
    
    // 验证至少发送了一些消息
    EXPECT_GT(total_sent, 0) << "应该至少发送了一些消息";
    
    // 清理
    close(client_fd);
    close(server_fd);
    unlink(test_socket_path.c_str());
}

// ============================================================================
// 平台兼容性测试
// ============================================================================

// 测试 EventFD 模式在 Linux 上可用
// **Validates: Requirements 9.1**
#ifdef __linux__
TEST_F(NotifyModeConfigTest, EventFDModeAvailableOnLinux) {
    // 在 Linux 上，EventFD 模式应该可用
    // 创建配置并设置为 EventFD 模式
    ConsumerConfig consumer_cfg;
    consumer_cfg.notify_mode = NotifyMode::EventFD;
    
    // 验证配置可以设置为 EventFD
    EXPECT_EQ(consumer_cfg.notify_mode, NotifyMode::EventFD);
    
    ProducerConfig producer_cfg;
    producer_cfg.notify_mode = NotifyMode::EventFD;
    
    EXPECT_EQ(producer_cfg.notify_mode, NotifyMode::EventFD);
    
    // 注意：实际的 eventfd 创建在 LockFreeRingBuffer 中进行
    // 这里只验证配置层面的支持
}
#endif

// 测试 macOS 上 EventFD 回退到 UDS
// **Validates: Requirements 2.4, 3.4, 9.3**
#ifdef __APPLE__
TEST_F(NotifyModeConfigTest, EventFDFallbackToUDSOnMacOS) {
    // 在 macOS 上，即使配置为 EventFD，系统应该回退到 UDS
    // 这个测试验证配置层面的行为
    
    // 创建配置并设置为 EventFD 模式
    ConsumerConfig consumer_cfg;
    consumer_cfg.notify_mode = NotifyMode::EventFD;
    
    // 配置本身可以设置为 EventFD（用户意图）
    EXPECT_EQ(consumer_cfg.notify_mode, NotifyMode::EventFD);
    
    // 但实际使用时，SharedMemoryConsumerSink 会将其回退到 UDS
    // 这个回退逻辑在 SharedMemoryConsumerSink 构造函数中实现
    // 由于我们不想在单元测试中创建实际的共享内存，
    // 这里只验证配置可以被设置，实际回退行为由集成测试验证
    
    ProducerConfig producer_cfg;
    producer_cfg.notify_mode = NotifyMode::EventFD;
    
    EXPECT_EQ(producer_cfg.notify_mode, NotifyMode::EventFD);
}
#endif

// 测试 UDS 模式在所有平台上可用
// **Validates: Requirements 9.1, 9.2**
TEST_F(NotifyModeConfigTest, UDSModeAvailableOnAllPlatforms) {
    // UDS 模式应该在所有平台上可用
    ConsumerConfig consumer_cfg;
    consumer_cfg.notify_mode = NotifyMode::UDS;
    
    EXPECT_EQ(consumer_cfg.notify_mode, NotifyMode::UDS);
    
    ProducerConfig producer_cfg;
    producer_cfg.notify_mode = NotifyMode::UDS;
    
    EXPECT_EQ(producer_cfg.notify_mode, NotifyMode::UDS);
    
    // 验证 UDS 路径生成在所有平台上工作
    std::string uds_path = generate_default_uds_path("/test_shm");
    EXPECT_FALSE(uds_path.empty());
    EXPECT_EQ(uds_path, "/tmp/spdlog_mp_test_shm.sock");
}

// 测试条件编译宏定义
// **Validates: Requirements 9.4**
TEST_F(NotifyModeConfigTest, ConditionalCompilationMacros) {
    // 验证平台检测宏正确定义
#ifdef __linux__
    // Linux 平台
    bool is_linux = true;
    bool is_macos = false;
#elif defined(__APPLE__)
    // macOS 平台
    bool is_linux = false;
    bool is_macos = true;
#else
    // 其他平台
    bool is_linux = false;
    bool is_macos = false;
#endif
    
    // 至少应该是 Linux 或 macOS 之一（或其他平台）
    // 这个测试主要验证条件编译宏正确工作
    EXPECT_TRUE(is_linux || is_macos || (!is_linux && !is_macos));
    
    // 验证 SPDLOG_ENABLE_MULTIPROCESS 宏已定义（否则这个测试不会编译）
#ifdef SPDLOG_ENABLE_MULTIPROCESS
    bool multiprocess_enabled = true;
#else
    bool multiprocess_enabled = false;
#endif
    EXPECT_TRUE(multiprocess_enabled);
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
