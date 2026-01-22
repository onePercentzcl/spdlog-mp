# Copyright(c) 2019 spdlog authors Distributed under the MIT License (http://opensource.org/licenses/MIT)

cmake_minimum_required(VERSION 3.11)
project(spdlog_examples CXX)

if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()

# ---------------------------------------------------------------------------------------
# Example of using pre-compiled library
# ---------------------------------------------------------------------------------------
add_executable(example example.cpp)
target_link_libraries(example PRIVATE spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)

# ---------------------------------------------------------------------------------------
# Example of using header-only library
# ---------------------------------------------------------------------------------------
if(SPDLOG_BUILD_EXAMPLE_HO)
    add_executable(example_header_only example.cpp)
    target_link_libraries(example_header_only PRIVATE spdlog::spdlog_header_only)
endif()

# ---------------------------------------------------------------------------------------
# Multiprocess examples (only when SPDLOG_ENABLE_MULTIPROCESS is enabled)
# ---------------------------------------------------------------------------------------
if(SPDLOG_ENABLE_MULTIPROCESS)
    if(UNIX)
        # Example MP1 - Non-fork multiprocess with UDS notification
        # Consumer process (main process)
        add_executable(example_mp1_consumer example_mp1_consumer.cpp)
        target_link_libraries(example_mp1_consumer PRIVATE spdlog::spdlog)
        set_target_properties(example_mp1_consumer PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
        
        # Producer process
        add_executable(example_mp1_producer example_mp1_producer.cpp)
        target_link_libraries(example_mp1_producer PRIVATE spdlog::spdlog)
        set_target_properties(example_mp1_producer PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
    endif()
endif()
