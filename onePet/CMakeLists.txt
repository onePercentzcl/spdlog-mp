cmake_minimum_required(VERSION 3.14)
project(onePet)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/onePercentzcl/spdlog-mp.git
        GIT_TAG v0.0.2
)
set(SPDLOG_ENABLE_MULTIPROCESS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

include_directories(include)

add_executable(onePet src/main.cpp
        include/main.h
        include/utils/logger.h
        src/utils/logger.cpp
        include/SCU/scu.h
        src/scu/scu.cpp
        include/dpu/dpu.h
        src/dpu/dpu.cpp
)
target_include_directories(onePet PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(onePet PRIVATE spdlog::spdlog)
