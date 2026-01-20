// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#ifndef SPDLOG_COMPILED_LIB
#error Please define SPDLOG_COMPILED_LIB to compile this file.
#endif

#include <mutex>

#include <spdlog/async.h>
#include <spdlog/details/null_mutex.h>
//
// color sink（输出目标）
//
#ifdef _WIN32
#include <spdlog/sinks/wincolor_sink-inl.h>
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_nullmutex>;
#else
#include "spdlog/sinks/ansicolor_sink-inl.h"
template class SPDLOG_API spdlog::sinks::ansicolor_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stdout_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stderr_sink<spdlog::details::console_nullmutex>;
#endif

// factory methods for color loggers
#include "spdlog/sinks/stdout_color_sinks-inl.h"
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_mt<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_st<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_mt<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_st<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     color_mode mode);

template SPDLOG_API std::shared_ptr<spdlog::logger> spdlog::stdout_color_mt<spdlog::async_factory>(
    const std::string &logger_name, color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger> spdlog::stdout_color_st<spdlog::async_factory>(
    const std::string &logger_name, color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger> spdlog::stderr_color_mt<spdlog::async_factory>(
    const std::string &logger_name, color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger> spdlog::stderr_color_st<spdlog::async_factory>(
    const std::string &logger_name, color_mode mode);
