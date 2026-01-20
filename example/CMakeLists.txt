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
        # Default config example - simplest usage with default configuration
        add_executable(multiprocess_default_example multiprocess_default_example.cpp)
        target_link_libraries(multiprocess_default_example PRIVATE spdlog::spdlog)
        set_target_properties(multiprocess_default_example PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
        
        # Fork example - demonstrates fork() with multiple child processes
        add_executable(multiprocess_fork_example multiprocess_fork_example.cpp)
        target_link_libraries(multiprocess_fork_example PRIVATE spdlog::spdlog)
        set_target_properties(multiprocess_fork_example PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
        
        # Separate process example - demonstrates independent processes
        add_executable(multiprocess_separate_example multiprocess_separate_example.cpp)
        target_link_libraries(multiprocess_separate_example PRIVATE spdlog::spdlog)
        set_target_properties(multiprocess_separate_example PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
        
        # Offset example - demonstrates using a portion of shared memory
        add_executable(multiprocess_offset_example multiprocess_offset_example.cpp)
        target_link_libraries(multiprocess_offset_example PRIVATE spdlog::spdlog)
        set_target_properties(multiprocess_offset_example PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
    endif()
endif()
