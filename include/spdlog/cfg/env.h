// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once
#include <spdlog/cfg/helpers.h>
#include <spdlog/details/os.h>
#include <spdlog/details/registry.h>

//
// Init levels and patterns from env variables SPDLOG_LEVEL
// Inspired from Rust's "env_logger" crate (https://crates.io/crates/env_logger).
// Note - fallback to "info" level on unrecognized levels
//
// Examples:
//
// set global level to debug:
// export SPDLOG_LEVEL=debug
//
// turn off all logging 除了 logger1:
// export SPDLOG_LEVEL="*=off,logger1=debug"
//

// turn off all logging 除了 logger1 and logger2:
// export SPDLOG_LEVEL="off,logger1=debug,logger2=info"

namespace spdlog {
namespace cfg {
inline void load_env_levels(const char* var = "SPDLOG_LEVEL") {
    const auto levels_spec = details::os::getenv(var);
    if (!levels_spec.empty()) {
        helpers::load_levels(levels_spec);
    }
}

}  // namespace cfg
}  // namespace spdlog
