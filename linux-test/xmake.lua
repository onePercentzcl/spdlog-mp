set_xmakever("2.8.0")

-- 使用本地 spdlog-mp 源码
add_includedirs("../include")
add_files("../src/*.cpp", "../src/multiprocess/*.cpp")
add_defines("SPDLOG_COMPILED_LIB", "SPDLOG_ENABLE_MULTIPROCESS")
add_syslinks("pthread")

target("test_app")
    set_kind("binary")
    set_languages("c++17")
    add_files("main.cpp")

target("test_eventfd")
    set_kind("binary")
    set_languages("c++17")
    add_files("test_eventfd.cpp")

target("test_polling_optimization")
    set_kind("binary")
    set_languages("c++17")
    add_files("test_polling_optimization.cpp")

target("test_poll_duration_config")
    set_kind("binary")
    set_languages("c++17")
    add_files("test_poll_duration_config.cpp")

target("test_polling_vs_notification")
    set_kind("binary")
    set_languages("c++17")
    add_files("test_polling_vs_notification.cpp")
