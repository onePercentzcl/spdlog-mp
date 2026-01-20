// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

// 孤立共享内存清理工具
// 用于检测并清理由于进程崩溃而遗留的共享内存段

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <set>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <cerrno>
    #include <pwd.h>
#endif

namespace {

// 获取注册表文件路径
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

// 确保注册表目录存在
bool ensure_registry_dir() {
    std::string path = get_registry_path();
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
std::set<std::string> read_registry() {
    std::set<std::string> names;
    std::string path = get_registry_path();
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
void write_registry(const std::set<std::string>& names) {
    ensure_registry_dir();
    std::string path = get_registry_path();
    if (path.empty()) return;
    
    std::ofstream file(path);
    if (!file.is_open()) return;
    
    for (const auto& name : names) {
        file << name << "\n";
    }
}

// 添加到注册表
void register_shm(const std::string& name) {
    auto names = read_registry();
    names.insert(name);
    write_registry(names);
}

// 从注册表移除
void unregister_shm(const std::string& name) {
    auto names = read_registry();
    names.erase(name);
    write_registry(names);
}

// 打印使用帮助
void print_usage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项] [共享内存名称...]\n"
              << "\n"
              << "选项:\n"
              << "  -h, --help     显示此帮助信息\n"
              << "  -l, --list     列出所有spdlog共享内存段\n"
              << "  -a, --all      清理所有spdlog共享内存段\n"
              << "  -f, --force    强制清理（不提示确认）\n"
              << "  -p, --prefix   指定共享内存名称前缀（默认: /spdlog_shm_）\n"
              << "\n"
              << "示例:\n"
              << "  " << program_name << " -l                    # 列出所有spdlog共享内存\n"
              << "  " << program_name << " -a                    # 清理所有spdlog共享内存\n"
              << "  " << program_name << " /spdlog_shm_abc123    # 清理指定的共享内存\n"
              << "\n"
              << "平台说明:\n"
              << "  Linux:  从/dev/shm目录列出共享内存对象\n"
              << "  macOS:  使用注册表文件跟踪共享内存 (~/.spdlog/shm_registry.txt)\n"
              << "          注册表由spdlog库自动维护\n"
              << "  Windows: 共享内存在所有句柄关闭后自动清理\n"
              << std::endl;
}

#ifndef _WIN32
// 列出所有spdlog共享内存段（POSIX系统）
std::vector<std::string> list_spdlog_shm(const std::string& prefix) {
    std::vector<std::string> result;
    
#ifdef __linux__
    // 在Linux上，共享内存对象位于/dev/shm目录
    DIR* dir = opendir("/dev/shm");
    if (dir == nullptr) {
        std::cerr << "无法打开/dev/shm目录: " << strerror(errno) << std::endl;
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // 检查是否以指定前缀开头（去掉开头的/）
        std::string prefix_without_slash = prefix.substr(1);  // 去掉开头的/
        if (name.find(prefix_without_slash) == 0) {
            result.push_back("/" + name);
        }
    }
    
    closedir(dir);
#else
    // macOS: 从注册表读取，并验证共享内存是否仍然存在
    std::cout << "注意: macOS使用注册表文件跟踪共享内存: " << get_registry_path() << std::endl;
    
    auto registered = read_registry();
    std::string prefix_without_slash = prefix.substr(1);
    
    for (const auto& name : registered) {
        // 检查是否匹配前缀
        std::string name_without_slash = name;
        if (name[0] == '/') {
            name_without_slash = name.substr(1);
        }
        
        if (name_without_slash.find(prefix_without_slash) == 0 || 
            prefix_without_slash.empty()) {
            // 验证共享内存是否仍然存在
            int fd = shm_open(name.c_str(), O_RDONLY, 0);
            if (fd != -1) {
                close(fd);
                result.push_back(name);
            }
        }
    }
#endif
    
    return result;
}

// 清理指定的共享内存（POSIX系统）
bool cleanup_shm(const std::string& name, bool force) {
    // 尝试打开共享内存
    int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd == -1) {
        if (errno == ENOENT) {
            std::cout << "共享内存 " << name << " 不存在";
            // 从注册表中移除
            unregister_shm(name);
            std::cout << " (已从注册表移除)" << std::endl;
            return true;
        }
        std::cerr << "无法打开共享内存 " << name << ": " << strerror(errno) << std::endl;
        return false;
    }
    close(fd);
    
    // 确认清理
    if (!force) {
        std::cout << "确定要清理共享内存 " << name << "? (y/n): ";
        char response;
        std::cin >> response;
        if (response != 'y' && response != 'Y') {
            std::cout << "已取消" << std::endl;
            return false;
        }
    }
    
    // 删除共享内存
    if (shm_unlink(name.c_str()) == -1) {
        std::cerr << "无法删除共享内存 " << name << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    // 从注册表移除
    unregister_shm(name);
    
    std::cout << "已清理共享内存: " << name << std::endl;
    return true;
}

#else // _WIN32

// Windows实现
std::vector<std::string> list_spdlog_shm(const std::string& prefix) {
    std::vector<std::string> result;
    // Windows不支持列出共享内存对象
    std::cout << "注意: Windows不支持列出共享内存对象，请指定具体名称" << std::endl;
    return result;
}

bool cleanup_shm(const std::string& name, bool force) {
    // Windows共享内存在所有句柄关闭后自动清理
    // 这里只是尝试打开并关闭
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str());
    if (hMapFile == NULL) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            std::cout << "共享内存 " << name << " 不存在" << std::endl;
            return true;
        }
        std::cerr << "无法打开共享内存 " << name << ": 错误码 " << error << std::endl;
        return false;
    }
    
    CloseHandle(hMapFile);
    std::cout << "注意: Windows共享内存在所有句柄关闭后自动清理" << std::endl;
    return true;
}

#endif // _WIN32

} // anonymous namespace

int main(int argc, char* argv[]) {
    bool list_mode = false;
    bool cleanup_all = false;
    bool force = false;
    std::string prefix = "/spdlog_shm_";
    std::vector<std::string> names_to_cleanup;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-l" || arg == "--list") {
            list_mode = true;
        } else if (arg == "-a" || arg == "--all") {
            cleanup_all = true;
        } else if (arg == "-f" || arg == "--force") {
            force = true;
        } else if (arg == "-p" || arg == "--prefix") {
            if (i + 1 < argc) {
                prefix = argv[++i];
            } else {
                std::cerr << "错误: -p/--prefix 需要一个参数" << std::endl;
                return 1;
            }
        } else if (arg[0] == '-') {
            std::cerr << "未知选项: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        } else {
            names_to_cleanup.push_back(arg);
        }
    }
    
    // 如果没有指定任何操作，显示帮助
    if (!list_mode && !cleanup_all && names_to_cleanup.empty()) {
        print_usage(argv[0]);
        return 0;
    }
    
    // 列出模式
    if (list_mode) {
        std::cout << "正在查找前缀为 " << prefix << " 的共享内存段..." << std::endl;
        auto shm_list = list_spdlog_shm(prefix);
        
        if (shm_list.empty()) {
            std::cout << "未找到spdlog共享内存段" << std::endl;
        } else {
            std::cout << "找到 " << shm_list.size() << " 个共享内存段:" << std::endl;
            for (const auto& name : shm_list) {
                std::cout << "  " << name << std::endl;
            }
        }
        
        if (!cleanup_all && names_to_cleanup.empty()) {
            return 0;
        }
    }
    
    // 清理所有模式
    if (cleanup_all) {
        auto shm_list = list_spdlog_shm(prefix);
        
        if (shm_list.empty()) {
            std::cout << "没有需要清理的共享内存段" << std::endl;
            return 0;
        }
        
        if (!force) {
            std::cout << "将清理 " << shm_list.size() << " 个共享内存段，确定吗? (y/n): ";
            char response;
            std::cin >> response;
            if (response != 'y' && response != 'Y') {
                std::cout << "已取消" << std::endl;
                return 0;
            }
        }
        
        int success_count = 0;
        for (const auto& name : shm_list) {
            if (cleanup_shm(name, true)) {  // 已经确认过了，强制清理
                success_count++;
            }
        }
        
        std::cout << "已清理 " << success_count << "/" << shm_list.size() << " 个共享内存段" << std::endl;
        return (success_count == static_cast<int>(shm_list.size())) ? 0 : 1;
    }
    
    // 清理指定的共享内存
    int success_count = 0;
    for (const auto& name : names_to_cleanup) {
        if (cleanup_shm(name, force)) {
            success_count++;
        }
    }
    
    if (!names_to_cleanup.empty()) {
        std::cout << "已清理 " << success_count << "/" << names_to_cleanup.size() << " 个共享内存段" << std::endl;
    }
    
    return (success_count == static_cast<int>(names_to_cleanup.size())) ? 0 : 1;
}
