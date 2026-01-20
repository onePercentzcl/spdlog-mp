// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#include <spdlog/common.h>
#include <unordered_map>

namespace spdlog {
namespace cfg {
namespace helpers {
//
// Init levels from given string
//
// Examples:
//
// set global level to debug: "debug"
// turn off all logging 除了 logger1: "off,logger1=debug"
// turn off all logging 除了 logger1 and logger2: "off,logger1=debug,logger2=info"
//
SPDLOG_API void load_levels(const std::string &levels_spec);
}  // namespace helpers

}  // namespace cfg
}  // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "helpers-inl.h"
#endif  // SPDLOG_HEADER_ONLY
