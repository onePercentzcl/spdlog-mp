# CMakeLists.txt for multiprocess tests
cmake_minimum_required(VERSION 3.11)

if(NOT SPDLOG_ENABLE_MULTIPROCESS)
    message(STATUS "Multiprocess tests skipped (SPDLOG_ENABLE_MULTIPROCESS not enabled)")
    return()
endif()

# Find or fetch Google Test
find_package(GTest QUIET)
if(GTest_FOUND)
    message(STATUS "Packaged version of Google Test will be used.")
else()
    message(STATUS "Bundled version of Google Test will be downloaded and used.")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Multiprocess test sources
set(MULTIPROCESS_TEST_SOURCES
    test_shared_memory_manager.cpp
    test_lock_free_ring_buffer.cpp
    test_shared_memory_producer_sink.cpp
    test_shared_memory_consumer_sink.cpp
    test_shared_memory_consumer_sink_properties.cpp
    test_alignment.cpp
    test_config.cpp
    test_error_handling.cpp
    test_resource_cleanup.cpp
    test_backward_compatibility.cpp
    test_performance.cpp
    test_integration.cpp
    test_notify_mode_config.cpp
)

# Create test executable
add_executable(spdlog-multiprocess-tests ${MULTIPROCESS_TEST_SOURCES})

# Require C++17 for multiprocess tests (for std::optional)
set_target_properties(spdlog-multiprocess-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Link libraries
target_link_libraries(spdlog-multiprocess-tests 
    PRIVATE 
    spdlog::spdlog
    GTest::gtest_main
)

# Enable warnings
if(COMMAND spdlog_enable_warnings)
    spdlog_enable_warnings(spdlog-multiprocess-tests)
endif()

# Add sanitizer support
if(SPDLOG_SANITIZE_ADDRESS)
    if(COMMAND spdlog_enable_addr_sanitizer)
        spdlog_enable_addr_sanitizer(spdlog-multiprocess-tests)
    endif()
elseif(SPDLOG_SANITIZE_THREAD)
    if(COMMAND spdlog_enable_thread_sanitizer)
        spdlog_enable_thread_sanitizer(spdlog-multiprocess-tests)
    endif()
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(spdlog-multiprocess-tests)
