// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#define SPDLOG_VER_MAJOR 1
#define SPDLOG_VER_MINOR 17
#define SPDLOG_VER_PATCH 0

#define SPDLOG_TO_VERSION(major, minor, patch) (major * 10000 + minor * 100 + patch)
#define SPDLOG_VERSION SPDLOG_TO_VERSION(SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH)
