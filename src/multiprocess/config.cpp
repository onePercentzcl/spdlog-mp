// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <spdlog/multiprocess/common.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <mutex>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <pwd.h>
#endif

namespace spdlog {
namespace config {

ConfigResult<SharedMemoryHandle> from_env(const std::string& var_name) {
    const char* env_value = std::getenv(var_name.c_str());
    if (env_value == nullptr || env_value[0] == '\0') {
        return ConfigResult<SharedMemoryHandle>();
    }
    
    // 环境变量格式: "name:size" 或 "name:size:fd"
    // 例如: "spdlog_shm:1048576" 或 "spdlog_shm:1048576:3"
    std::string value_str(env_value);
    
    size_t first_colon = value_str.find(':');
    if (first_colon == std::string::npos) {
        return ConfigResult<SharedMemoryHandle>();
    }
    
    std::string name = value_str.substr(0, first_colon);
    std::string rest = value_str.substr(first_colon + 1);
    
    size_t second_colon = rest.find(':');
    std::string size_str;
    std::string fd_str;
    
    if (second_colon != std::string::npos) {
        size_str = rest.substr(0, second_colon);
        fd_str = rest.substr(second_colon + 1);
    } else {
        size_str = rest;
    }
    
    // 解析大小
    char* end_ptr = nullptr;
    size_t size = std::strtoull(size_str.c_str(), &end_ptr, 10);
    if (end_ptr == size_str.c_str() || size == 0) {
        return ConfigResult<SharedMemoryHandle>();
    }
    
    // 解析文件描述符（如果存在）
    int fd = -1;
    if (!fd_str.empty()) {
        fd = std::atoi(fd_str.c_str());
    }
    
    return ConfigResult<SharedMemoryHandle>(SharedMemoryHandle(fd, name, size));
}

ConfigResult<SharedMemoryHandle> from_args(int argc, char** argv, const std::string& arg_name) {
    if (argc < 2 || argv == nullptr) {
        return ConfigResult<SharedMemoryHandle>();
    }
    
    // 查找参数: --shm-name=value 或 --shm-name value
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        
        // 检查 --arg-name=value 格式
        if (arg.find(arg_name + "=") == 0) {
            std::string value = arg.substr(arg_name.length() + 1);
            if (value.empty()) {
                continue;
            }
            
            // 解析值（格式同环境变量）
            size_t first_colon = value.find(':');
            if (first_colon == std::string::npos) {
                // 只有名称，没有大小
                continue;
            }
            
            std::string name = value.substr(0, first_colon);
            std::string rest = value.substr(first_colon + 1);
            
            size_t second_colon = rest.find(':');
            std::string size_str;
            std::string fd_str;
            
            if (second_colon != std::string::npos) {
                size_str = rest.substr(0, second_colon);
                fd_str = rest.substr(second_colon + 1);
            } else {
                size_str = rest;
            }
            
            char* end_ptr = nullptr;
            size_t size = std::strtoull(size_str.c_str(), &end_ptr, 10);
            if (end_ptr == size_str.c_str() || size == 0) {
                continue;
            }
            
            int fd = -1;
            if (!fd_str.empty()) {
                fd = std::atoi(fd_str.c_str());
            }
            
            return ConfigResult<SharedMemoryHandle>(SharedMemoryHandle(fd, name, size));
        }
        
        // 检查 --arg-name value 格式
        if (arg == arg_name && i + 1 < argc) {
            std::string value(argv[i + 1]);
            
            size_t first_colon = value.find(':');
            if (first_colon == std::string::npos) {
                continue;
            }
            
            std::string name = value.substr(0, first_colon);
            std::string rest = value.substr(first_colon + 1);
            
            size_t second_colon = rest.find(':');
            std::string size_str;
            std::string fd_str;
            
            if (second_colon != std::string::npos) {
                size_str = rest.substr(0, second_colon);
                fd_str = rest.substr(second_colon + 1);
            } else {
                size_str = rest;
            }
            
            char* end_ptr = nullptr;
            size_t size = std::strtoull(size_str.c_str(), &end_ptr, 10);
            if (end_ptr == size_str.c_str() || size == 0) {
                continue;
            }
            
            int fd = -1;
            if (!fd_str.empty()) {
                fd = std::atoi(fd_str.c_str());
            }
            
            return ConfigResult<SharedMemoryHandle>(SharedMemoryHandle(fd, name, size));
        }
    }
    
    return ConfigResult<SharedMemoryHandle>();
}

} // namespace config

namespace shm_registry {

namespace {
    // 全局互斥锁保护注册表操作
    std::mutex registry_mutex;
    
    // 确保注册表目录存在
    bool ensure_registry_dir(const std::string& path) {
        if (path.empty()) return false;
        
        // 获取目录部分
        size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos) return false;
        
        std::string dir = path.substr(0, pos);
        
#ifdef _WIN32
        CreateDirectoryA(dir.c_str(), NULL);
#else
        mkdir(dir.c_str(), 0755);
#endif
        return true;
    }
    
    // 从注册表读取共享内存名称
    std::set<std::string> read_registry_internal(const std::string& path) {
        std::set<std::string> names;
        if (path.empty()) return names;
        
        std::ifstream file(path);
        if (!file.is_open()) return names;
        
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                names.insert(line);
            }
        }
        return names;
    }
    
    // 写入注册表
    void write_registry_internal(const std::string& path, const std::set<std::string>& names) {
        ensure_registry_dir(path);
        if (path.empty()) return;
        
        std::ofstream file(path);
        if (!file.is_open()) return;
        
        for (const auto& name : names) {
            file << name << "\n";
        }
    }
} // anonymous namespace

std::string get_registry_path() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\spdlog\\shm_registry.txt";
    }
    return "";
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    if (home) {
        return std::string(home) + "/.spdlog/shm_registry.txt";
    }
    return "/tmp/.spdlog_shm_registry.txt";
#endif
}

void register_shm(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    std::string path = get_registry_path();
    auto names = read_registry_internal(path);
    names.insert(name);
    write_registry_internal(path, names);
}

void unregister_shm(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    std::string path = get_registry_path();
    auto names = read_registry_internal(path);
    names.erase(name);
    write_registry_internal(path, names);
}

} // namespace shm_registry
} // namespace spdlog

#endif // SPDLOG_ENABLE_MULTIPROCESS
