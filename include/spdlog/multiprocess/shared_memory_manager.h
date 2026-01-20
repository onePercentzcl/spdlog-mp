// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/common.h>

namespace spdlog {

// 共享内存管理器
class SharedMemoryManager {
public:
    // 创建新的共享内存段
    // @param size: 共享内存大小（字节）
    // @param name: 共享内存名称（可选）
    // @return: 成功返回标识符，失败返回错误码
    static Result<SharedMemoryHandle> create(size_t size, const std::string& name = "");
    
    // 映射到现有共享内存
    // @param handle: 共享内存标识符（文件描述符或名称）
    // @return: 成功返回映射的内存指针，失败返回错误码
    static Result<void*> attach(const SharedMemoryHandle& handle);
    
    // 映射到现有共享内存并检查版本兼容性
    // @param handle: 共享内存标识符（文件描述符或名称）
    // @return: 成功返回映射的内存指针，版本不匹配或失败返回错误码
    static Result<void*> attach_with_version_check(const SharedMemoryHandle& handle);
    
    // 从共享内存分离
    // @param ptr: 映射的内存指针
    // @param size: 映射的大小
    static void detach(void* ptr, size_t size);
    
    // 销毁共享内存段（仅消费者调用）
    // @param handle: 共享内存标识符
    static void destroy(const SharedMemoryHandle& handle);
    
    // 重新初始化共享内存（调整大小）
    // @param handle: 现有共享内存标识符
    // @param new_size: 新的大小
    // @return: 成功返回新的标识符，失败返回错误码
    static Result<SharedMemoryHandle> reinitialize(const SharedMemoryHandle& handle, size_t new_size);
    
    // 验证共享内存标识符
    // @param handle: 待验证的标识符
    // @return: 有效返回true，否则返回false
    static bool validate(const SharedMemoryHandle& handle);

private:
    // 内部映射实现
    // @param handle: 共享内存标识符
    // @param check_version: 是否检查版本兼容性
    // @return: 成功返回映射的内存指针，失败返回错误码
    static Result<void*> attach_internal(const SharedMemoryHandle& handle, bool check_version);
};

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
