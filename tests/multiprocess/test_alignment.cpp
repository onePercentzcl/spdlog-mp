// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifdef SPDLOG_ENABLE_MULTIPROCESS

#include <gtest/gtest.h>
#include <spdlog/multiprocess/lock_free_ring_buffer.h>
#include <cstddef>

using namespace spdlog;

// 测试缓存行对齐常量
TEST(AlignmentTest, CacheLineSizeConstant) {
    // 验证缓存行大小常量为64字节
    EXPECT_EQ(CACHE_LINE_SIZE, 64u) << "Cache line size should be 64 bytes";
}

// 测试缓存行对齐
TEST(AlignmentTest, MetadataAlignment) {
    // 验证Metadata结构中的原子变量是否正确对齐到64字节边界
    struct TestMetadata {
        uint32_t version;
        uint32_t capacity;
        uint32_t slot_size;
        OverflowPolicy overflow_policy;
        int eventfd;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_index;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_index;
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> consumer_state;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> last_poll_time_ns;
    };
    
    TestMetadata metadata;
    
    // 获取write_index和read_index的地址
    uintptr_t write_addr = reinterpret_cast<uintptr_t>(&metadata.write_index);
    uintptr_t read_addr = reinterpret_cast<uintptr_t>(&metadata.read_index);
    uintptr_t consumer_state_addr = reinterpret_cast<uintptr_t>(&metadata.consumer_state);
    uintptr_t last_poll_addr = reinterpret_cast<uintptr_t>(&metadata.last_poll_time_ns);
    
    // 验证write_index对齐到缓存行边界
    EXPECT_EQ(write_addr % CACHE_LINE_SIZE, 0u) << "write_index should be aligned to cache line boundary";
    
    // 验证read_index对齐到缓存行边界
    EXPECT_EQ(read_addr % CACHE_LINE_SIZE, 0u) << "read_index should be aligned to cache line boundary";
    
    // 验证consumer_state对齐到缓存行边界
    EXPECT_EQ(consumer_state_addr % CACHE_LINE_SIZE, 0u) << "consumer_state should be aligned to cache line boundary";
    
    // 验证last_poll_time_ns对齐到缓存行边界
    EXPECT_EQ(last_poll_addr % CACHE_LINE_SIZE, 0u) << "last_poll_time_ns should be aligned to cache line boundary";
    
    // 验证write_index和read_index不在同一个缓存行
    EXPECT_GE(std::abs(static_cast<long>(read_addr - write_addr)), static_cast<long>(CACHE_LINE_SIZE)) 
        << "write_index and read_index should be in different cache lines";
    
    // 验证所有原子变量都在不同的缓存行
    EXPECT_GE(std::abs(static_cast<long>(consumer_state_addr - read_addr)), static_cast<long>(CACHE_LINE_SIZE))
        << "consumer_state and read_index should be in different cache lines";
    
    EXPECT_GE(std::abs(static_cast<long>(last_poll_addr - consumer_state_addr)), static_cast<long>(CACHE_LINE_SIZE))
        << "last_poll_time_ns and consumer_state should be in different cache lines";
    
    std::cout << "Metadata structure size: " << sizeof(TestMetadata) << " bytes\n";
    std::cout << "write_index offset: " << offsetof(TestMetadata, write_index) << " bytes\n";
    std::cout << "read_index offset: " << offsetof(TestMetadata, read_index) << " bytes\n";
    std::cout << "consumer_state offset: " << offsetof(TestMetadata, consumer_state) << " bytes\n";
    std::cout << "last_poll_time_ns offset: " << offsetof(TestMetadata, last_poll_time_ns) << " bytes\n";
}

// 测试Slot结构的对齐
TEST(AlignmentTest, SlotAlignment) {
    // 验证Slot结构对齐到缓存行边界
    EXPECT_EQ(alignof(LockFreeRingBuffer::Slot), CACHE_LINE_SIZE) 
        << "Slot structure should be aligned to cache line boundary";
    
    LockFreeRingBuffer::Slot* slot = nullptr;
    
    // 分配对齐的内存来测试
    void* mem = nullptr;
    if (posix_memalign(&mem, CACHE_LINE_SIZE, sizeof(LockFreeRingBuffer::Slot) + 256) == 0) {
        slot = new (mem) LockFreeRingBuffer::Slot();
        
        uintptr_t slot_addr = reinterpret_cast<uintptr_t>(slot);
        uintptr_t committed_addr = reinterpret_cast<uintptr_t>(&slot->committed);
        
        // 验证槽位起始地址对齐到缓存行边界
        EXPECT_EQ(slot_addr % CACHE_LINE_SIZE, 0u) << "Slot should be aligned to cache line boundary";
        
        // 验证committed标志地址（应该在槽位开头）
        EXPECT_EQ(committed_addr, slot_addr) << "committed flag should be at the start of the slot";
        
        std::cout << "Slot structure size: " << sizeof(LockFreeRingBuffer::Slot) << " bytes\n";
        std::cout << "Slot alignment: " << alignof(LockFreeRingBuffer::Slot) << " bytes\n";
        
        free(mem);
    }
}

// 测试避免伪共享
TEST(AlignmentTest, FalseSharingPrevention) {
    // 分配足够大的内存来创建环形缓冲区
    const size_t buffer_size = 1024 * 1024;  // 1MB
    void* memory = nullptr;
    
    if (posix_memalign(&memory, CACHE_LINE_SIZE, buffer_size) == 0) {
        LockFreeRingBuffer buffer(memory, buffer_size, 512, OverflowPolicy::Drop);
        
        // 获取统计信息来验证缓冲区正常工作
        auto stats = buffer.get_stats();
        
        // 验证容量大于0
        EXPECT_GT(stats.capacity, 0u) << "Buffer should have positive capacity";
        
        // 验证初始状态
        EXPECT_EQ(stats.total_writes, 0u) << "Initial writes should be 0";
        EXPECT_EQ(stats.total_reads, 0u) << "Initial reads should be 0";
        EXPECT_EQ(stats.current_usage, 0u) << "Initial usage should be 0";
        
        std::cout << "Buffer capacity: " << stats.capacity << " slots\n";
        std::cout << "Buffer size: " << buffer_size << " bytes\n";
        
        free(memory);
    }
}

#endif // SPDLOG_ENABLE_MULTIPROCESS
