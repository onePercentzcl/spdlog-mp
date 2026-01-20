// 版权所有(c) 2015至今, Gabi Melman 及 spdlog 贡献者。
// 根据 MIT 许可证分发 (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
#include <spdlog/async_logger.h>
#endif

#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/sink.h>

#include <memory>
#include <string>

SPDLOG_INLINE spdlog::async_logger::async_logger(std::string logger_name,
                                                 sinks_init_list sinks_list,
                                                 std::weak_ptr<details::thread_pool> tp,
                                                 async_overflow_policy overflow_policy)
    : async_logger(std::move(logger_name),
                   sinks_list.begin(),
                   sinks_list.end(),
                   std::move(tp),
                   overflow_policy) {}

SPDLOG_INLINE spdlog::async_logger::async_logger(std::string logger_name,
                                                 sink_ptr single_sink,
                                                 std::weak_ptr<details::thread_pool> tp,
                                                 async_overflow_policy overflow_policy)
    : async_logger(
          std::move(logger_name), {std::move(single_sink)}, std::move(tp), overflow_policy) {}

// 将日志消息发送到线程池
SPDLOG_INLINE void spdlog::async_logger::sink_it_(const details::log_msg &msg){
    SPDLOG_TRY{if (auto pool_ptr = thread_pool_.lock()){
        pool_ptr -> post_log(shared_from_this(), msg, overflow_policy_);
}
else {
    throw_spdlog_ex("async log: thread pool doesn't exist anymore");
}
}
SPDLOG_LOGGER_CATCH(msg.source)
}

// 向线程池发送刷新请求
SPDLOG_INLINE void spdlog::async_logger::flush_(){
    SPDLOG_TRY{if (auto pool_ptr = thread_pool_.lock()){
        pool_ptr -> post_flush(shared_from_this(), overflow_policy_);
}
else {
    throw_spdlog_ex("async flush: thread pool doesn't exist anymore");
}
}
SPDLOG_LOGGER_CATCH(source_loc())
}

//
// 后端函数 - 从线程池调用以执行实际工作
//
SPDLOG_INLINE void spdlog::async_logger::backend_sink_it_(const details::log_msg &incoming_log_msg) {
    for (auto &sink : sinks_) {
        if (sink->should_log(incoming_log_msg.level)) {
            SPDLOG_TRY { sink->log(incoming_log_msg); }
            SPDLOG_LOGGER_CATCH(incoming_log_msg.source)
        }
    }

    if (should_flush_(incoming_log_msg)) {
        backend_flush_();
    }
}

SPDLOG_INLINE void spdlog::async_logger::backend_flush_() {
    for (auto &sink : sinks_) {
        SPDLOG_TRY { sink->flush(); }
        SPDLOG_LOGGER_CATCH(source_loc())
    }
}

SPDLOG_INLINE std::shared_ptr<spdlog::logger> spdlog::async_logger::clone(std::string new_name) {
    auto cloned = std::make_shared<spdlog::async_logger>(*this);
    cloned->name_ = std::move(new_name);
    return cloned;
}
