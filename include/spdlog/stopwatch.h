// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#include <chrono>
#include <spdlog/fmt/fmt.h>

// spdlog 的秒表支持（使用 std::chrono::steady_clock）。
// 将自构造以来经过的秒数显示为 double。
//
// 用法:
//
// spdlog::stopwatch sw;
// ...
// spdlog::debug("Elapsed: {} seconds", sw);    =>  "Elapsed 0.005116733 seconds"
// spdlog::info("Elapsed: {:.6} seconds", sw);  =>  "Elapsed 0.005163 seconds"
//
//
// 如果需要其他单位（例如毫秒而不是 double），请包含 "fmt/chrono.h" 并使用
// "duration_cast<..>(sw.elapsed())":
//
// #include <spdlog/fmt/chrono.h>
//..
// using std::chrono::duration_cast;
// using std::chrono::milliseconds;
// spdlog::info("Elapsed {}", duration_cast<milliseconds>(sw.elapsed())); => "Elapsed 5ms"

namespace spdlog {
class stopwatch {
    using clock = std::chrono::steady_clock;
    std::chrono::time_point<clock> start_tp_;

public:
    stopwatch()
        : start_tp_{clock::now()} {}

    std::chrono::duration<double> elapsed() const {
        return std::chrono::duration<double>(clock::now() - start_tp_);
    }

    std::chrono::milliseconds elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start_tp_);
    }

    void reset() { start_tp_ = clock::now(); }
};
}  // namespace spdlog

// 支持 fmt 格式化（例如 "{:012.9}" 或仅 "{}"))
namespace
#ifdef SPDLOG_USE_STD_FORMAT
    std
#else
    fmt
#endif
{

template <>
struct formatter<spdlog::stopwatch> : formatter<double> {
    template <typename FormatContext>
    auto format(const spdlog::stopwatch &sw, FormatContext &ctx) const -> decltype(ctx.out()) {
        return formatter<double>::format(sw.elapsed().count(), ctx);
    }
};
}  // namespace std
