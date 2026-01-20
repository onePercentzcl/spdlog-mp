// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

/**
 * @file daily_file_sink.h
 * @brief 基于日期的每日轮转文件 sink
 * 
 * @details
 * 提供了按日期自动轮转日志文件的功能，每天在指定时间创建新的日志文件。
 * 这是生产环境中最常用的文件 sink 之一。
 * 
 * **核心特性**：
 * - 基于日期和时间自动轮转
 * - 支持自定义轮转时间（小时和分钟）
 * - 自动生成带日期的文件名
 * - 可选保留指定天数的历史文件
 * - 支持自定义文件名格式（使用 strftime）
 * - 支持多线程和单线程版本
 * 
 * **轮转机制**：
 * - 每天在指定时间（如 00:00）自动创建新文件
 * - 文件名自动包含日期信息
 * - 可选择性删除过期的旧文件
 * 
 * **文件命名规则**：
 * - 默认格式: basename_YYYY-MM-DD.ext
 * - 自定义格式: 使用 strftime 格式字符串
 * 
 * **使用场景**：
 * - 长期运行的应用程序
 * - 需要按日期组织日志的场景
 * - 需要保留一定天数历史日志的场景
 * - 日志审计和分析
 * 
 * @note 如果需要按文件大小轮转，请使用 rotating_file_sink
 * @see rotating_file_sink 按文件大小轮转的 sink
 * 
 * @par 使用示例
 * @code
 * // 创建每日文件 logger，每天 00:00 轮转
 * auto logger = spdlog::daily_logger_mt("daily_logger", "logs/daily.log", 0, 0);
 * 
 * // 文件名示例：
 * // logs/daily_2026-01-19.log
 * // logs/daily_2026-01-20.log
 * // logs/daily_2026-01-21.log
 * 
 * // 每天 23:59 轮转，保留 7 天的日志
 * auto logger2 = spdlog::daily_logger_mt("daily_logger2", "logs/daily.log", 
 *                                        23, 59, false, 7);
 * 
 * // 使用自定义格式
 * auto logger3 = spdlog::daily_logger_format_mt("daily_logger3", 
 *                                               "logs/app-%Y%m%d-%H%M%S.log",
 *                                               0, 0);
 * @endcode
 */

#include <spdlog/common.h>
#include <spdlog/details/circular_q.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/base_sink.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace spdlog {
namespace sinks {

/**
 * @struct daily_filename_calculator
 * @brief 默认的每日文件名生成器
 * 
 * @details
 * 生成格式为 basename_YYYY-MM-DD.ext 的文件名。
 * 
 * **文件名格式**：
 * - 输入: "logs/app.log"
 * - 输出: "logs/app_2026-01-19.log"
 * 
 * @par 使用示例
 * @code
 * tm now_tm = ...; // 当前时间
 * auto filename = daily_filename_calculator::calc_filename("logs/app.log", now_tm);
 * // 返回: "logs/app_2026-01-19.log"
 * @endcode
 */
struct daily_filename_calculator {
    /**
     * @brief 计算带日期的文件名
     * 
     * @details
     * 将基础文件名和日期组合成完整的文件名。
     * 格式: basename_YYYY-MM-DD.ext
     * 
     * @param filename 基础文件名（如 "logs/app.log"）
     * @param now_tm 当前时间结构
     * @return 带日期的完整文件名
     */
    static filename_t calc_filename(const filename_t &filename, const tm &now_tm) {
        filename_t basename, ext;
        std::tie(basename, ext) = details::file_helper::split_by_extension(filename);
        return fmt_lib::format(SPDLOG_FMT_STRING(SPDLOG_FILENAME_T("{}_{:04d}-{:02d}-{:02d}{}")),
                               basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
                               ext);
    }
};

/**
 * @struct daily_filename_format_calculator
 * @brief 使用 strftime 格式的文件名生成器
 * 
 * @details
 * 使用 strftime 格式字符串生成自定义格式的文件名。
 * 提供了更灵活的文件名定制能力。
 * 
 * **支持的格式标志**：
 * - %Y: 年份（4位）
 * - %m: 月份（01-12）
 * - %d: 日期（01-31）
 * - %H: 小时（00-23）
 * - %M: 分钟（00-59）
 * - %S: 秒（00-59）
 * - 更多格式请参考 strftime 文档
 * 
 * @par 使用示例
 * @code
 * // 使用自定义格式创建 logger
 * auto logger = spdlog::daily_logger_format_mt("logger", 
 *                                              "logs/app-%Y%m%d-%H%M%S.log",
 *                                              0, 0);
 * // 文件名示例: "logs/app-20260119-000000.log"
 * @endcode
 */
struct daily_filename_format_calculator {
    /**
     * @brief 使用 strftime 格式计算文件名
     * 
     * @details
     * 将 strftime 格式字符串和时间结构组合成文件名。
     * 
     * @param file_path strftime 格式的文件路径模板
     * @param now_tm 当前时间结构
     * @return 格式化后的文件名
     */
    static filename_t calc_filename(const filename_t &file_path, const tm &now_tm) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
        std::wstringstream stream;
#else
        std::stringstream stream;
#endif
        stream << std::put_time(&now_tm, file_path.c_str());
        return stream.str();
    }
};

/**
 * @class daily_file_sink
 * @brief 基于日期的每日轮转文件 sink 类
 * 
 * @details
 * 这是一个按日期自动轮转的文件 sink，在指定时间创建新的日志文件。
 * 继承自 base_sink，自动处理线程同步和格式化。
 * 
 * **核心功能**：
 * - 监控当前时间
 * - 在指定时间自动轮转文件
 * - 生成带日期的文件名
 * - 可选删除过期文件
 * 
 * **轮转策略**：
 * - 每天在指定时间（小时:分钟）创建新文件
 * - 文件名自动包含日期信息
 * - 可选保留指定天数的历史文件
 * - 自动删除超出天数的旧文件
 * 
 * **线程安全性**：
 * - 通过模板参数 Mutex 控制
 * - _mt 版本：线程安全
 * - _st 版本：单线程，性能更好
 * 
 * **文件名生成**：
 * - 通过模板参数 FileNameCalc 控制
 * - daily_filename_calculator: 默认格式（basename_YYYY-MM-DD.ext）
 * - daily_filename_format_calculator: strftime 格式
 * 
 * @tparam Mutex 互斥锁类型
 * @tparam FileNameCalc 文件名计算器类型
 * 
 * @note 这是一个 final 类，不能被继承
 * @warning 旧的日志文件（程序启动前的）不会被自动删除，只有程序运行期间创建的文件才会被管理
 */
template <typename Mutex, typename FileNameCalc = daily_filename_calculator>
class daily_file_sink final : public base_sink<Mutex> {
public:
    /**
     * @brief 构造函数
     * 
     * @details
     * 创建一个每日轮转文件 sink，配置轮转时间和文件保留策略。
     * 
     * **参数说明**：
     * - base_filename: 基础文件名，会自动添加日期
     * - rotation_hour: 轮转时间的小时（0-23）
     * - rotation_minute: 轮转时间的分钟（0-59）
     * - truncate: 是否截断现有文件
     * - max_files: 保留的最大文件数量（0 表示不限制）
     * 
     * **轮转时间**：
     * - 每天在 rotation_hour:rotation_minute 创建新文件
     * - 例如：rotation_hour=0, rotation_minute=0 表示每天午夜轮转
     * 
     * **文件保留**：
     * - max_files = 0: 不删除旧文件
     * - max_files > 0: 只保留最近 max_files 天的文件
     * 
     * @param base_filename 基础文件名（不包括日期）
     * @param rotation_hour 轮转时间的小时（0-23）
     * @param rotation_minute 轮转时间的分钟（0-59）
     * @param truncate 是否截断现有文件（默认为 false）
     * @param max_files 保留的最大文件数量（默认为 0，不限制）
     * @param event_handlers 文件事件处理器（可选）
     * 
     * @throws spdlog_ex 如果轮转时间无效或无法创建文件
     * 
     * @par 使用示例
     * @code
     * // 每天午夜轮转，不限制文件数量
     * auto sink1 = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
     *     "logs/daily.log", 0, 0);
     * 
     * // 每天 23:59 轮转，保留 7 天
     * auto sink2 = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
     *     "logs/daily.log", 23, 59, false, 7);
     * 
     * // 每天 12:00 轮转，截断模式
     * auto sink3 = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
     *     "logs/daily.log", 12, 0, true);
     * @endcode
     */
    daily_file_sink(filename_t base_filename,
                    int rotation_hour,
                    int rotation_minute,
                    bool truncate = false,
                    uint16_t max_files = 0,
                    const file_event_handlers &event_handlers = {})
        : base_filename_(std::move(base_filename)),
          rotation_h_(rotation_hour),
          rotation_m_(rotation_minute),
          file_helper_{event_handlers},
          truncate_(truncate),
          max_files_(max_files),
          filenames_q_() {
        if (rotation_hour < 0 || rotation_hour > 23 || rotation_minute < 0 ||
            rotation_minute > 59) {
            throw_spdlog_ex("daily_file_sink: Invalid rotation time in ctor");
        }

        auto now = log_clock::now();
        const auto new_filename = FileNameCalc::calc_filename(base_filename_, now_tm(now));
        file_helper_.open(new_filename, truncate_);
        rotation_tp_ = next_rotation_tp_();

        if (max_files_ > 0) {
            init_filenames_q_();
        }
    }

    /**
     * @brief 获取当前文件名
     * 
     * @details
     * 返回当前正在写入的日志文件名（包含日期）。
     * 
     * @return 当前文件名
     * 
     * @par 使用示例
     * @code
     * auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
     *     "logs/daily.log", 0, 0);
     * std::cout << "当前文件: " << sink->filename() << std::endl;
     * // 输出: "当前文件: logs/daily_2026-01-19.log"
     * @endcode
     */
    filename_t filename() {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        return file_helper_.filename();
    }

protected:
    /**
     * @brief 实际的日志写入操作
     * 
     * @details
     * 将格式化后的日志消息写入文件，并检查是否需要轮转。
     * 
     * **实现细节**：
     * - 检查当前时间是否达到轮转时间
     * - 如果需要轮转，创建新文件
     * - 格式化并写入日志消息
     * - 如果启用了文件数量限制，删除过期文件
     * 
     * @param msg 要写入的日志消息
     * 
     * @note 这是一个 protected 方法，由 base_sink 调用
     */
    void sink_it_(const details::log_msg &msg) override {
        auto time = msg.time;
        bool should_rotate = time >= rotation_tp_;
        if (should_rotate) {
            const auto new_filename = FileNameCalc::calc_filename(base_filename_, now_tm(time));
            file_helper_.open(new_filename, truncate_);
            rotation_tp_ = next_rotation_tp_();
        }
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        file_helper_.write(formatted);

        // Do the cleaning only at the end because it might throw on failure.
        if (should_rotate && max_files_ > 0) {
            delete_old_();
        }
    }

    /**
     * @brief 实际的刷新操作
     * 
     * @details
     * 将文件缓冲区中的所有内容立即写入磁盘。
     * 
     * @note 这是一个 protected 方法，由 base_sink 调用
     */
    void flush_() override { file_helper_.flush(); }

private:
    /**
     * @brief 初始化文件名队列
     * 
     * @details
     * 扫描现有的日志文件，将它们添加到文件名队列中。
     * 这样可以在程序启动时正确管理已存在的日志文件。
     * 
     * @note 这是一个私有方法，由构造函数调用
     */
    void init_filenames_q_() {
        using details::os::path_exists;

        filenames_q_ = details::circular_q<filename_t>(static_cast<size_t>(max_files_));
        std::vector<filename_t> filenames;
        auto now = log_clock::now();
        while (filenames.size() < max_files_) {
            const auto new_filename = FileNameCalc::calc_filename(base_filename_, now_tm(now));
            if (!path_exists(new_filename)) {
                break;
            }
            filenames.emplace_back(new_filename);
            now -= std::chrono::hours(24);
        }
        for (auto iter = filenames.rbegin(); iter != filenames.rend(); ++iter) {
            filenames_q_.push_back(std::move(*iter));
        }
    }

    /**
     * @brief 将时间点转换为 tm 结构
     * 
     * @details
     * 将 log_clock::time_point 转换为本地时间的 tm 结构。
     * 
     * @param tp 时间点
     * @return tm 结构
     * 
     * @note 这是一个私有方法，用于时间转换
     */
    tm now_tm(log_clock::time_point tp) {
        time_t tnow = log_clock::to_time_t(tp);
        return spdlog::details::os::localtime(tnow);
    }

    /**
     * @brief 计算下次轮转时间
     * 
     * @details
     * 根据当前时间和配置的轮转时间，计算下次应该轮转的时间点。
     * 
     * **计算逻辑**：
     * - 如果今天的轮转时间还未到，返回今天的轮转时间
     * - 如果今天的轮转时间已过，返回明天的轮转时间
     * 
     * @return 下次轮转的时间点
     * 
     * @note 这是一个私有方法，用于计算轮转时间
     */
    log_clock::time_point next_rotation_tp_() {
        auto now = log_clock::now();
        tm date = now_tm(now);
        date.tm_hour = rotation_h_;
        date.tm_min = rotation_m_;
        date.tm_sec = 0;
        auto rotation_time = log_clock::from_time_t(std::mktime(&date));
        if (rotation_time > now) {
            return rotation_time;
        }
        return {rotation_time + std::chrono::hours(24)};
    }

    /**
     * @brief 删除过期的旧文件
     * 
     * @details
     * 当文件数量超过限制时，删除最旧的文件。
     * 
     * **删除策略**：
     * - 维护一个循环队列，记录所有文件名
     * - 当队列满时，删除最旧的文件
     * - 将当前文件添加到队列
     * 
     * @throws spdlog_ex 如果删除文件失败
     * 
     * @note 这是一个私有方法，由 sink_it_() 调用
     */
    void delete_old_() {
        using details::os::filename_to_str;
        using details::os::remove_if_exists;

        filename_t current_file = file_helper_.filename();
        if (filenames_q_.full()) {
            auto old_filename = std::move(filenames_q_.front());
            filenames_q_.pop_front();
            bool ok = remove_if_exists(old_filename) == 0;
            if (!ok) {
                filenames_q_.push_back(std::move(current_file));
                throw_spdlog_ex("Failed removing daily file " + filename_to_str(old_filename),
                                errno);
            }
        }
        filenames_q_.push_back(std::move(current_file));
    }

    filename_t base_filename_;                      ///< 基础文件名
    int rotation_h_;                                ///< 轮转时间的小时
    int rotation_m_;                                ///< 轮转时间的分钟
    log_clock::time_point rotation_tp_;             ///< 下次轮转的时间点
    details::file_helper file_helper_;              ///< 文件助手对象
    bool truncate_;                                 ///< 是否截断文件
    uint16_t max_files_;                            ///< 保留的最大文件数量
    details::circular_q<filename_t> filenames_q_;   ///< 文件名循环队列
};

/**
 * @typedef daily_file_sink_mt
 * @brief 多线程安全的每日文件 sink（默认格式）
 * 
 * @details
 * 使用 std::mutex 保护，文件名格式为 basename_YYYY-MM-DD.ext。
 * 这是最常用的每日文件 sink 类型。
 */
using daily_file_sink_mt = daily_file_sink<std::mutex>;

/**
 * @typedef daily_file_sink_st
 * @brief 单线程的每日文件 sink（默认格式）
 * 
 * @details
 * 不使用互斥锁，性能更好，但只能在单线程环境使用。
 */
using daily_file_sink_st = daily_file_sink<details::null_mutex>;

/**
 * @typedef daily_file_format_sink_mt
 * @brief 多线程安全的每日文件 sink（自定义格式）
 * 
 * @details
 * 使用 strftime 格式字符串生成文件名。
 * 提供了更灵活的文件名定制能力。
 */
using daily_file_format_sink_mt = daily_file_sink<std::mutex, daily_filename_format_calculator>;

/**
 * @typedef daily_file_format_sink_st
 * @brief 单线程的每日文件 sink（自定义格式）
 * 
 * @details
 * 使用 strftime 格式字符串生成文件名，单线程版本。
 */
using daily_file_format_sink_st =
    daily_file_sink<details::null_mutex, daily_filename_format_calculator>;

}  // namespace sinks

/**
 * @brief 创建多线程安全的每日文件 logger（默认格式）
 * 
 * @details
 * 这是一个工厂函数，创建一个按日期自动轮转的文件 logger。
 * 文件名格式为 basename_YYYY-MM-DD.ext。
 * 
 * **特点**：
 * - 线程安全
 * - 每天在指定时间自动轮转
 * - 文件名自动包含日期
 * - 可选保留指定天数的历史文件
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 基础文件名
 * @param hour 轮转时间的小时（0-23，默认为 0）
 * @param minute 轮转时间的分钟（0-59，默认为 0）
 * @param truncate 是否截断现有文件（默认为 false）
 * @param max_files 保留的最大文件数量（默认为 0，不限制）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果轮转时间无效或无法创建文件
 * 
 * @par 使用示例
 * @code
 * // 每天午夜轮转
 * auto logger = spdlog::daily_logger_mt("daily_logger", "logs/daily.log");
 * // 文件名: logs/daily_2026-01-19.log
 * 
 * // 每天 23:59 轮转，保留 7 天
 * auto logger2 = spdlog::daily_logger_mt("daily_logger2", "logs/daily.log", 
 *                                        23, 59, false, 7);
 * 
 * logger->info("日志消息");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_mt(const std::string &logger_name,
                                               const filename_t &filename,
                                               int hour = 0,
                                               int minute = 0,
                                               bool truncate = false,
                                               uint16_t max_files = 0,
                                               const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::daily_file_sink_mt>(logger_name, filename, hour, minute,
                                                               truncate, max_files, event_handlers);
}

/**
 * @brief 创建多线程安全的每日文件 logger（自定义格式）
 * 
 * @details
 * 创建一个使用 strftime 格式字符串的每日文件 logger。
 * 提供了更灵活的文件名定制能力。
 * 
 * **支持的格式标志**：
 * - %Y: 年份（4位）
 * - %m: 月份（01-12）
 * - %d: 日期（01-31）
 * - %H: 小时（00-23）
 * - %M: 分钟（00-59）
 * - %S: 秒（00-59）
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename strftime 格式的文件名模板
 * @param hour 轮转时间的小时（0-23，默认为 0）
 * @param minute 轮转时间的分钟（0-59，默认为 0）
 * @param truncate 是否截断现有文件（默认为 false）
 * @param max_files 保留的最大文件数量（默认为 0，不限制）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果轮转时间无效或无法创建文件
 * 
 * @par 使用示例
 * @code
 * // 使用自定义格式
 * auto logger = spdlog::daily_logger_format_mt("daily_logger",
 *                                              "logs/app-%Y%m%d-%H%M%S.log",
 *                                              0, 0);
 * // 文件名: logs/app-20260119-000000.log
 * 
 * logger->info("日志消息");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_format_mt(
    const std::string &logger_name,
    const filename_t &filename,
    int hour = 0,
    int minute = 0,
    bool truncate = false,
    uint16_t max_files = 0,
    const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::daily_file_format_sink_mt>(
        logger_name, filename, hour, minute, truncate, max_files, event_handlers);
}

/**
 * @brief 创建单线程的每日文件 logger（默认格式）
 * 
 * @details
 * 创建一个按日期自动轮转的单线程文件 logger。
 * 性能更好，但不是线程安全的。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename 基础文件名
 * @param hour 轮转时间的小时（0-23，默认为 0）
 * @param minute 轮转时间的分钟（0-59，默认为 0）
 * @param truncate 是否截断现有文件（默认为 false）
 * @param max_files 保留的最大文件数量（默认为 0，不限制）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果轮转时间无效或无法创建文件
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto logger = spdlog::daily_logger_st("daily_logger", "logs/daily.log");
 * logger->info("单线程每日日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_st(const std::string &logger_name,
                                               const filename_t &filename,
                                               int hour = 0,
                                               int minute = 0,
                                               bool truncate = false,
                                               uint16_t max_files = 0,
                                               const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::daily_file_sink_st>(logger_name, filename, hour, minute,
                                                               truncate, max_files, event_handlers);
}

/**
 * @brief 创建单线程的每日文件 logger（自定义格式）
 * 
 * @details
 * 创建一个使用 strftime 格式字符串的单线程每日文件 logger。
 * 
 * @tparam Factory logger 工厂类型，默认为同步工厂
 * @param logger_name logger 的名称
 * @param filename strftime 格式的文件名模板
 * @param hour 轮转时间的小时（0-23，默认为 0）
 * @param minute 轮转时间的分钟（0-59，默认为 0）
 * @param truncate 是否截断现有文件（默认为 false）
 * @param max_files 保留的最大文件数量（默认为 0，不限制）
 * @param event_handlers 文件事件处理器（可选）
 * @return logger 的共享指针
 * 
 * @throws spdlog_ex 如果轮转时间无效或无法创建文件
 * @warning 只能在单线程环境使用
 * 
 * @par 使用示例
 * @code
 * auto logger = spdlog::daily_logger_format_st("daily_logger",
 *                                              "logs/app-%Y%m%d.log",
 *                                              0, 0);
 * logger->info("单线程自定义格式每日日志");
 * @endcode
 */
template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_format_st(
    const std::string &logger_name,
    const filename_t &filename,
    int hour = 0,
    int minute = 0,
    bool truncate = false,
    uint16_t max_files = 0,
    const file_event_handlers &event_handlers = {}) {
    return Factory::template create<sinks::daily_file_format_sink_st>(
        logger_name, filename, hour, minute, truncate, max_files, event_handlers);
}
}  // namespace spdlog

