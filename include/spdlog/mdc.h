// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#if defined(SPDLOG_NO_TLS)
#error "This header requires thread local storage support, but SPDLOG_NO_TLS is defined."
#endif

#include <map>
#include <string>

#include <spdlog/common.h>

// MDC 是存储在线程本地存储中的简单键->字符串值映射，其内容将被
// logger 打印。注意: 异步模式不支持（线程本地存储 - 因此异步
// 线程池有不同的副本）。
//
// 使用示例:
// spdlog::mdc::put("mdc_key_1", "mdc_value_1");
// spdlog::info("Hello, {}", "World!");  // => [2024-04-26 02:08:05.040] [info]
// [mdc_key_1:mdc_value_1] Hello, World!

namespace spdlog {
class SPDLOG_API mdc {
public:
    using mdc_map_t = std::map<std::string, std::string>;

    static void put(const std::string &key, const std::string &value) {
        get_context()[key] = value;
    }

    static std::string get(const std::string &key) {
        auto &context = get_context();
        auto it = context.find(key);
        if (it != context.end()) {
            return it->second;
        }
        return "";
    }

    static void remove(const std::string &key) { get_context().erase(key); }

    static void clear() { get_context().clear(); }

    static mdc_map_t &get_context() {
        static thread_local mdc_map_t context;
        return context;
    }
};

}  // namespace spdlog
