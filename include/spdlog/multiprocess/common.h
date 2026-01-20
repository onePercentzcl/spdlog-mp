// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/common.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace spdlog {

// 版本号
constexpr uint32_t MULTIPROCESS_VERSION = 1;

// 溢出策略
enum class OverflowPolicy {
    Block,  // 阻塞直到有空间
    Drop    // 丢弃新消息
};

// 共享内存标识符
struct SharedMemoryHandle {
    int fd;                    // POSIX文件描述符 (Linux/macOS)
    std::string name;          // 共享内存名称
    size_t size;               // 大小
    
    SharedMemoryHandle() : fd(-1), size(0) {}
    SharedMemoryHandle(int fd_, const std::string& name_, size_t size_)
        : fd(fd_), name(name_), size(size_) {}
};

// 结果类型（用于错误处理）
template<typename T>
class Result {
public:
    static Result<T> ok(T value) {
        Result<T> r;
        r.ok_ = true;
        r.value_ = std::move(value);
        return r;
    }
    
    static Result<T> error(const std::string& message) {
        Result<T> r;
        r.ok_ = false;
        r.error_msg_ = message;
        return r;
    }
    
    bool is_ok() const { return ok_; }
    bool is_error() const { return !ok_; }
    
    const T& value() const { return value_; }
    T& value() { return value_; }
    
    const std::string& error_message() const { return error_msg_; }

private:
    bool ok_ = false;
    T value_;
    std::string error_msg_;
};

// 缓冲区统计信息
struct BufferStats {
    uint64_t total_writes = 0;      // 总写入次数
    uint64_t total_reads = 0;       // 总读取次数
    uint64_t dropped_messages = 0;  // 丢弃的消息数
    uint64_t current_usage = 0;     // 当前使用的槽位数
    uint64_t capacity = 0;          // 总容量
};

// 配置辅助函数命名空间
namespace config {

// 配置结果包装器（简单且跨C++标准兼容）
template<typename T>
struct ConfigResult {
    bool valid;
    T value;
    
    ConfigResult() : valid(false), value() {}
    explicit ConfigResult(const T& v) : valid(true), value(v) {}
    explicit ConfigResult(T&& v) : valid(true), value(std::move(v)) {}
    
    bool has_value() const { return valid; }
    explicit operator bool() const { return valid; }
    const T* operator->() const { return &value; }
    T* operator->() { return &value; }
};

// 从环境变量读取共享内存标识符
// @param var_name: 环境变量名称
// @return: 成功返回SharedMemoryHandle，失败返回空
ConfigResult<SharedMemoryHandle> from_env(const std::string& var_name);

// 从命令行参数读取共享内存标识符
// @param argc: 参数数量
// @param argv: 参数数组
// @param arg_name: 参数名称（例如 "--shm-name"）
// @return: 成功返回SharedMemoryHandle，失败返回空
ConfigResult<SharedMemoryHandle> from_args(int argc, char** argv, const std::string& arg_name = "--shm-name");

} // namespace config

// 共享内存注册表命名空间（用于跟踪创建的共享内存，支持macOS清理）
namespace shm_registry {

// 获取注册表文件路径
std::string get_registry_path();

// 注册共享内存名称
void register_shm(const std::string& name);

// 注销共享内存名称
void unregister_shm(const std::string& name);

} // namespace shm_registry

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
