// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <spdlog/details/log_msg.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <thread>
#include <atomic>
#include <cstring>
#include <iomanip>

using namespace spdlog;

// 性能测试套件
class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 分配共享内存用于测试
        buffer_size_ = 16 * 1024 * 1024; // 16MB for performance tests
        shared_memory_ = std::malloc(buffer_size_);
        ASSERT_NE(shared_memory_, nullptr);
    }

    void TearDown() override {
        if (shared_memory_) {
            std::free(shared_memory_);
            shared_memory_ = nullptr;
        }
    }
    
    // 辅助函数：创建测试用的log_msg
    details::log_msg create_test_log_msg(const std::string& message, 
                                         level::level_enum lvl = level::info,
                                         const std::string& logger_name = "perf_test") {
        return details::log_msg(
            log_clock::now(),
            source_loc{},
            string_view_t(logger_name),
            lvl,
            string_view_t(message)
        );
    }
    
    // 计算百分位数
    template<typename T>
    T percentile(std::vector<T>& data, double p) {
        if (data.empty()) return T{};
        std::sort(data.begin(), data.end());
        size_t idx = static_cast<size_t>(p * (data.size() - 1));
        return data[idx];
    }

    void* shared_memory_ = nullptr;
    size_t buffer_size_ = 0;
};

// **性能测试：吞吐量测试**
// **验证：需求 11.1**
TEST_F(PerformanceTest, ThroughputTest) {
    std::memset(shared_memory_, 0, buffer_size_);
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
    auto stats = buffer.get_stats();
    size_t capacity = stats.capacity;
    
    // 测试消息
    std::string test_message = "Performance test message with some content for realistic testing";
    auto log_msg = create_test_log_msg(test_message);
    
    // 预热
    for (size_t i = 0; i < 1000 && i < capacity; ++i) {
        auto reserve_result = buffer.reserve_slot();
        if (reserve_result.is_ok()) {
            buffer.write_slot(reserve_result.value(), log_msg);
            buffer.commit_slot(reserve_result.value());
        }
    }
    
    // 读取预热消息
    while (buffer.is_next_slot_committed()) {
        std::vector<char> read_buffer(slot_size);
        buffer.read_next_slot(read_buffer.data(), read_buffer.size());
        buffer.release_slot();
    }
    
    // 正式测试：写入吞吐量
    const size_t num_messages = std::min(capacity, static_cast<size_t>(100000));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t successful_writes = 0;
    for (size_t i = 0; i < num_messages; ++i) {
        auto reserve_result = buffer.try_reserve_slot();
        if (reserve_result.is_ok()) {
            buffer.write_slot(reserve_result.value(), log_msg);
            buffer.commit_slot(reserve_result.value());
            successful_writes++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = successful_writes / seconds;
    
    std::cout << "\n=== Write Throughput Test ===" << std::endl;
    std::cout << "Messages written: " << successful_writes << std::endl;
    std::cout << "Duration: " << duration.count() << " us" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) << throughput << " msg/sec" << std::endl;
    
    // 验证吞吐量合理（至少100,000 msg/sec）
    EXPECT_GT(throughput, 100000.0) << "Write throughput should be at least 100,000 msg/sec";
    
    // 读取吞吐量测试
    start = std::chrono::high_resolution_clock::now();
    
    size_t successful_reads = 0;
    std::vector<char> read_buffer(slot_size);
    while (buffer.is_next_slot_committed()) {
        auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
        if (read_result.is_ok()) {
            buffer.release_slot();
            successful_reads++;
        }
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    seconds = duration.count() / 1000000.0;
    double read_throughput = successful_reads / seconds;
    
    std::cout << "\n=== Read Throughput Test ===" << std::endl;
    std::cout << "Messages read: " << successful_reads << std::endl;
    std::cout << "Duration: " << duration.count() << " us" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) << read_throughput << " msg/sec" << std::endl;
    
    // 验证读取吞吐量合理
    EXPECT_GT(read_throughput, 100000.0) << "Read throughput should be at least 100,000 msg/sec";
}

// **性能测试：延迟测试（P50、P95、P99）**
// **验证：需求 11.2**
TEST_F(PerformanceTest, LatencyTest) {
    std::memset(shared_memory_, 0, buffer_size_);
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
    auto stats = buffer.get_stats();
    size_t capacity = stats.capacity;
    
    std::string test_message = "Latency test message";
    auto log_msg = create_test_log_msg(test_message);
    
    // 收集写入延迟
    const size_t num_samples = std::min(capacity / 2, static_cast<size_t>(10000));
    std::vector<int64_t> write_latencies;
    write_latencies.reserve(num_samples);
    
    for (size_t i = 0; i < num_samples; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto reserve_result = buffer.try_reserve_slot();
        if (reserve_result.is_ok()) {
            buffer.write_slot(reserve_result.value(), log_msg);
            buffer.commit_slot(reserve_result.value());
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        write_latencies.push_back(latency);
    }
    
    // 计算写入延迟百分位数
    int64_t write_p50 = percentile(write_latencies, 0.50);
    int64_t write_p95 = percentile(write_latencies, 0.95);
    int64_t write_p99 = percentile(write_latencies, 0.99);
    int64_t write_avg = std::accumulate(write_latencies.begin(), write_latencies.end(), 0LL) / write_latencies.size();
    
    std::cout << "\n=== Write Latency Test ===" << std::endl;
    std::cout << "Samples: " << write_latencies.size() << std::endl;
    std::cout << "Average: " << write_avg << " ns" << std::endl;
    std::cout << "P50: " << write_p50 << " ns" << std::endl;
    std::cout << "P95: " << write_p95 << " ns" << std::endl;
    std::cout << "P99: " << write_p99 << " ns" << std::endl;
    
    // 验证写入延迟合理（P99 < 100us = 100,000ns）
    EXPECT_LT(write_p99, 100000) << "Write P99 latency should be less than 100us";
    
    // 收集读取延迟
    std::vector<int64_t> read_latencies;
    read_latencies.reserve(num_samples);
    
    std::vector<char> read_buffer(slot_size);
    while (buffer.is_next_slot_committed() && read_latencies.size() < num_samples) {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto read_result = buffer.read_next_slot(read_buffer.data(), read_buffer.size());
        if (read_result.is_ok()) {
            buffer.release_slot();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        read_latencies.push_back(latency);
    }
    
    // 计算读取延迟百分位数
    int64_t read_p50 = percentile(read_latencies, 0.50);
    int64_t read_p95 = percentile(read_latencies, 0.95);
    int64_t read_p99 = percentile(read_latencies, 0.99);
    int64_t read_avg = std::accumulate(read_latencies.begin(), read_latencies.end(), 0LL) / read_latencies.size();
    
    std::cout << "\n=== Read Latency Test ===" << std::endl;
    std::cout << "Samples: " << read_latencies.size() << std::endl;
    std::cout << "Average: " << read_avg << " ns" << std::endl;
    std::cout << "P50: " << read_p50 << " ns" << std::endl;
    std::cout << "P95: " << read_p95 << " ns" << std::endl;
    std::cout << "P99: " << read_p99 << " ns" << std::endl;
    
    // 验证读取延迟合理
    EXPECT_LT(read_p99, 100000) << "Read P99 latency should be less than 100us";
}

// **性能测试：资源使用测试**
// **验证：需求 11.1, 11.2**
TEST_F(PerformanceTest, ResourceUsageTest) {
    std::memset(shared_memory_, 0, buffer_size_);
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
    auto stats = buffer.get_stats();
    
    std::cout << "\n=== Resource Usage Test ===" << std::endl;
    std::cout << "Buffer size: " << buffer_size_ / 1024 / 1024 << " MB" << std::endl;
    std::cout << "Slot size: " << slot_size << " bytes" << std::endl;
    std::cout << "Capacity: " << stats.capacity << " slots" << std::endl;
    std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
    
    // 验证内存使用效率
    size_t theoretical_capacity = buffer_size_ / slot_size;
    double efficiency = static_cast<double>(stats.capacity) / theoretical_capacity * 100;
    
    std::cout << "Theoretical capacity: " << theoretical_capacity << " slots" << std::endl;
    std::cout << "Memory efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    
    // 验证内存效率至少50%（考虑元数据和对齐开销）
    EXPECT_GT(efficiency, 50.0) << "Memory efficiency should be at least 50%";
    
    // 测试内存访问模式
    std::string test_message = "Resource test message";
    auto log_msg = create_test_log_msg(test_message);
    
    // 写入一半容量
    size_t half_capacity = stats.capacity / 2;
    for (size_t i = 0; i < half_capacity; ++i) {
        auto reserve_result = buffer.try_reserve_slot();
        if (reserve_result.is_ok()) {
            buffer.write_slot(reserve_result.value(), log_msg);
            buffer.commit_slot(reserve_result.value());
        }
    }
    
    // 验证使用量
    stats = buffer.get_stats();
    EXPECT_EQ(stats.current_usage, half_capacity) << "Current usage should match written messages";
    
    std::cout << "After writing " << half_capacity << " messages:" << std::endl;
    std::cout << "  Current usage: " << stats.current_usage << " slots" << std::endl;
    std::cout << "  Total writes: " << stats.total_writes << std::endl;
}

// **性能测试：多线程写入吞吐量**
// **验证：需求 11.1**
TEST_F(PerformanceTest, MultiThreadedWriteThroughput) {
    std::memset(shared_memory_, 0, buffer_size_);
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
    auto stats = buffer.get_stats();
    size_t capacity = stats.capacity;
    
    const int num_threads = 4;
    const size_t messages_per_thread = std::min(capacity / num_threads / 2, static_cast<size_t>(10000));
    
    std::atomic<size_t> total_successful_writes{0};
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::string test_message = "Thread " + std::to_string(t) + " message";
            auto log_msg = create_test_log_msg(test_message);
            
            size_t successful = 0;
            for (size_t i = 0; i < messages_per_thread; ++i) {
                auto reserve_result = buffer.try_reserve_slot();
                if (reserve_result.is_ok()) {
                    buffer.write_slot(reserve_result.value(), log_msg);
                    buffer.commit_slot(reserve_result.value());
                    successful++;
                }
            }
            total_successful_writes += successful;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = total_successful_writes / seconds;
    
    std::cout << "\n=== Multi-threaded Write Throughput Test ===" << std::endl;
    std::cout << "Threads: " << num_threads << std::endl;
    std::cout << "Messages per thread: " << messages_per_thread << std::endl;
    std::cout << "Total successful writes: " << total_successful_writes << std::endl;
    std::cout << "Duration: " << duration.count() << " us" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) << throughput << " msg/sec" << std::endl;
    
    // 验证多线程吞吐量合理
    EXPECT_GT(throughput, 50000.0) << "Multi-threaded write throughput should be at least 50,000 msg/sec";
}

// **性能测试：常数时间操作验证**
// **验证：需求 11.1, 11.2**
TEST_F(PerformanceTest, ConstantTimeOperations) {
    std::memset(shared_memory_, 0, buffer_size_);
    
    size_t slot_size = 512;
    LockFreeRingBuffer buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
    auto stats = buffer.get_stats();
    size_t capacity = stats.capacity;
    
    std::string test_message = "Constant time test message";
    auto log_msg = create_test_log_msg(test_message);
    
    // 测试不同填充级别下的写入延迟
    std::vector<std::pair<double, int64_t>> fill_level_latencies;
    
    std::vector<double> fill_levels = {0.1, 0.25, 0.5, 0.75, 0.9};
    
    for (double fill_level : fill_levels) {
        // 重置缓冲区
        std::memset(shared_memory_, 0, buffer_size_);
        LockFreeRingBuffer test_buffer(shared_memory_, buffer_size_, slot_size, OverflowPolicy::Drop);
        
        // 填充到指定级别
        size_t target_fill = static_cast<size_t>(capacity * fill_level);
        for (size_t i = 0; i < target_fill; ++i) {
            auto reserve_result = test_buffer.try_reserve_slot();
            if (reserve_result.is_ok()) {
                test_buffer.write_slot(reserve_result.value(), log_msg);
                test_buffer.commit_slot(reserve_result.value());
            }
        }
        
        // 测量写入延迟
        const size_t num_samples = 1000;
        std::vector<int64_t> latencies;
        latencies.reserve(num_samples);
        
        for (size_t i = 0; i < num_samples; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            auto reserve_result = test_buffer.try_reserve_slot();
            if (reserve_result.is_ok()) {
                test_buffer.write_slot(reserve_result.value(), log_msg);
                test_buffer.commit_slot(reserve_result.value());
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            latencies.push_back(latency);
            
            // 读取一条消息以保持填充级别
            if (test_buffer.is_next_slot_committed()) {
                std::vector<char> read_buffer(slot_size);
                test_buffer.read_next_slot(read_buffer.data(), read_buffer.size());
                test_buffer.release_slot();
            }
        }
        
        int64_t avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0LL) / latencies.size();
        fill_level_latencies.push_back({fill_level, avg_latency});
    }
    
    std::cout << "\n=== Constant Time Operations Test ===" << std::endl;
    std::cout << "Fill Level | Avg Latency (ns)" << std::endl;
    std::cout << "-----------|------------------" << std::endl;
    
    for (const auto& [fill, latency] : fill_level_latencies) {
        std::cout << std::fixed << std::setprecision(0) << std::setw(9) << (fill * 100) << "% | " 
                  << std::setw(16) << latency << std::endl;
    }
    
    // 验证延迟在不同填充级别下相对稳定（O(1)操作）
    // 允许2倍的变化范围
    int64_t min_latency = fill_level_latencies[0].second;
    int64_t max_latency = fill_level_latencies[0].second;
    
    for (const auto& [fill, latency] : fill_level_latencies) {
        min_latency = std::min(min_latency, latency);
        max_latency = std::max(max_latency, latency);
    }
    
    double latency_ratio = static_cast<double>(max_latency) / min_latency;
    std::cout << "\nLatency ratio (max/min): " << std::fixed << std::setprecision(2) << latency_ratio << std::endl;
    
    // 验证延迟比率在合理范围内（允许3倍变化，考虑系统噪声）
    EXPECT_LT(latency_ratio, 3.0) << "Latency should be relatively constant (O(1)) across fill levels";
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
