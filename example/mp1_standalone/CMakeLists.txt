# CMakeLists.txt for spdlog-mp example_mp1
# 从远程仓库下载 spdlog-mp 库

cmake_minimum_required(VERSION 3.14)
project(example_mp1 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 FetchContent 下载 spdlog-mp
include(FetchContent)
FetchContent_Declare(
    spdlog-mp
    GIT_REPOSITORY https://github.com/onePercentzcl/spdlog-mp.git
    GIT_TAG v0.0.1
)
FetchContent_MakeAvailable(spdlog-mp)

# 消费者程序
add_executable(example_mp1_consumer example_mp1_consumer.cpp)
target_link_libraries(example_mp1_consumer PRIVATE spdlog::spdlog)
target_compile_definitions(example_mp1_consumer PRIVATE SPDLOG_ENABLE_MULTIPROCESS)

# 生产者程序
add_executable(example_mp1_producer example_mp1_producer.cpp)
target_link_libraries(example_mp1_producer PRIVATE spdlog::spdlog)
target_compile_definitions(example_mp1_producer PRIVATE SPDLOG_ENABLE_MULTIPROCESS)

# Linux 需要链接 pthread 和 rt
if(UNIX AND NOT APPLE)
    target_link_libraries(example_mp1_consumer PRIVATE pthread rt)
    target_link_libraries(example_mp1_producer PRIVATE pthread rt)
elseif(APPLE)
    target_link_libraries(example_mp1_consumer PRIVATE pthread)
    target_link_libraries(example_mp1_producer PRIVATE pthread)
endif()
