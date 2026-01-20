// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/shared_memory_manager.h>
#include <spdlog/common.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>
#endif

#include <random>
#include <sstream>
#include <iomanip>

namespace spdlog {

namespace {
    // 生成随机的共享内存名称
    std::string generate_shm_name() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::stringstream ss;
        ss << "/spdlog_shm_";
        for (int i = 0; i < 16; ++i) {
            ss << std::hex << dis(gen);
        }
        return ss.str();
    }
}

// 创建新的共享内存段
Result<SharedMemoryHandle> SharedMemoryManager::create(size_t size, const std::string& name) {
    if (size == 0) {
        return Result<SharedMemoryHandle>::error("Invalid size: size must be greater than 0");
    }
    
    std::string shm_name = name.empty() ? generate_shm_name() : name;
    
#ifdef _WIN32
    // Windows实现
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,    // 使用页面文件
        NULL,                    // 默认安全属性
        PAGE_READWRITE,          // 读写访问
        0,                       // 高位大小
        static_cast<DWORD>(size), // 低位大小
        shm_name.c_str()         // 共享内存名称
    );
    
    if (hMapFile == NULL) {
        return Result<SharedMemoryHandle>::error(
            "CreateFileMapping failed: " + std::to_string(GetLastError()));
    }
    
    SharedMemoryHandle handle;
    handle.fd = reinterpret_cast<intptr_t>(hMapFile);
    handle.name = shm_name;
    handle.size = size;
    
    return Result<SharedMemoryHandle>::ok(handle);
    
#else
    // POSIX实现 (Linux/macOS)
    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd == -1) {
        return Result<SharedMemoryHandle>::error(
            "shm_open failed: " + std::string(std::strerror(errno)));
    }
    
    // 设置共享内存大小
    if (ftruncate(fd, static_cast<off_t>(size)) == -1) {
        int err = errno;
        close(fd);
        shm_unlink(shm_name.c_str());
        return Result<SharedMemoryHandle>::error(
            "ftruncate failed: " + std::string(std::strerror(err)));
    }
    
    SharedMemoryHandle handle;
    handle.fd = fd;
    handle.name = shm_name;
    handle.size = size;
    
    // 注册共享内存名称（用于macOS清理工具）
    shm_registry::register_shm(shm_name);
    
    return Result<SharedMemoryHandle>::ok(handle);
#endif
}

// 映射到现有共享内存
Result<void*> SharedMemoryManager::attach(const SharedMemoryHandle& handle) {
    // 不检查版本的内部映射
    return attach_internal(handle, false);
}

// 映射到现有共享内存并检查版本兼容性
Result<void*> SharedMemoryManager::attach_with_version_check(const SharedMemoryHandle& handle) {
    return attach_internal(handle, true);
}

// 内部映射实现
Result<void*> SharedMemoryManager::attach_internal(const SharedMemoryHandle& handle, bool check_version) {
    if (!SharedMemoryManager::validate(handle)) {
        return Result<void*>::error("Invalid shared memory handle");
    }
    
#ifdef _WIN32
    // Windows实现
    HANDLE hMapFile = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle.fd));
    void* ptr = MapViewOfFile(
        hMapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        handle.size
    );
    
    if (ptr == NULL) {
        return Result<void*>::error(
            "MapViewOfFile failed: " + std::to_string(GetLastError()));
    }
    
#else
    // POSIX实现
    void* ptr = mmap(
        NULL,
        handle.size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        handle.fd,
        0
    );
    
    if (ptr == MAP_FAILED) {
        return Result<void*>::error(
            "mmap failed: " + std::string(std::strerror(errno)));
    }
#endif
    
    // 版本兼容性检查
    if (check_version) {
        // 读取共享内存中的版本号
        uint32_t* version_ptr = static_cast<uint32_t*>(ptr);
        uint32_t stored_version = *version_ptr;
        
        if (stored_version != MULTIPROCESS_VERSION) {
            // 版本不匹配，取消映射并返回错误
#ifdef _WIN32
            UnmapViewOfFile(ptr);
#else
            munmap(ptr, handle.size);
#endif
            return Result<void*>::error(
                "Version mismatch: expected " + std::to_string(MULTIPROCESS_VERSION) + 
                ", got " + std::to_string(stored_version));
        }
    }
    
    return Result<void*>::ok(ptr);
}

// 从共享内存分离
void SharedMemoryManager::detach(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return;
    }
    
#ifdef _WIN32
    UnmapViewOfFile(ptr);
    (void)size; // 避免未使用参数警告
#else
    munmap(ptr, size);
#endif
}

// 销毁共享内存段
void SharedMemoryManager::destroy(const SharedMemoryHandle& handle) {
    if (!SharedMemoryManager::validate(handle)) {
        return;
    }
    
#ifdef _WIN32
    HANDLE hMapFile = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle.fd));
    CloseHandle(hMapFile);
#else
    close(handle.fd);
    shm_unlink(handle.name.c_str());
#endif
    
    // 从注册表移除
    shm_registry::unregister_shm(handle.name);
}

// 重新初始化共享内存
Result<SharedMemoryHandle> SharedMemoryManager::reinitialize(
    const SharedMemoryHandle& handle, size_t new_size) {
    
    if (!SharedMemoryManager::validate(handle)) {
        return Result<SharedMemoryHandle>::error("Invalid shared memory handle");
    }
    
    if (new_size == 0) {
        return Result<SharedMemoryHandle>::error("Invalid size: size must be greater than 0");
    }
    
    // 销毁旧的共享内存
    destroy(handle);
    
    // 创建新的共享内存（使用相同的名称）
    return create(new_size, handle.name);
}

// 验证共享内存标识符
bool SharedMemoryManager::validate(const SharedMemoryHandle& handle) {
#ifdef _WIN32
    return handle.fd != -1 && handle.size > 0;
#else
    return handle.fd >= 0 && handle.size > 0 && !handle.name.empty();
#endif
}

} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
