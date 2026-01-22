cmake_minimum_required(VERSION 3.14)
project(example_mp1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 FetchContent 从 GitHub 下载 spdlog-mp
include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/onePercentzcl/spdlog-mp.git
    GIT_TAG        main
)

# 启用多进程支持
set(SPDLOG_ENABLE_MULTIPROCESS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)

# 消费者程序
add_executable(example_mp1_consumer example_mp1_consumer.cpp)
target_link_libraries(example_mp1_consumer PRIVATE spdlog::spdlog pthread rt)

# 生产者程序
add_executable(example_mp1_producer example_mp1_producer.cpp)
target_link_libraries(example_mp1_producer PRIVATE spdlog::spdlog pthread rt)
