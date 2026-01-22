-- xmake.lua for spdlog
-- Copyright(c) 2019 spdlog authors Distributed under the MIT License (http://opensource.org/licenses/MIT)

set_project("spdlog")
set_version("1.17.0")

-- 设置默认构建模式
set_defaultmode("release")

-- 设置C++标准
set_languages("cxx17")

-- 配置编译模式
add_rules("mode.debug", "mode.release")

-- 确保 release 模式定义 NDEBUG
if is_mode("release") then
    add_defines("NDEBUG")
end

-- 配置选项
option("build_shared")
    set_default(false)
    set_showmenu(true)
    set_description("Build shared library")
option_end()

option("enable_pch")
    set_default(false)
    set_showmenu(true)
    set_description("Build using precompiled header to speed up compilation time")
option_end()

option("build_pic")
    set_default(false)
    set_showmenu(true)
    set_description("Build position independent code (-fPIC)")
option_end()

option("build_example")
    set_default(false)
    set_showmenu(true)
    set_description("Build example")
option_end()

option("build_example_ho")
    set_default(false)
    set_showmenu(true)
    set_description("Build header only example")
option_end()

option("build_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Build tests")
option_end()

option("build_tests_ho")
    set_default(false)
    set_showmenu(true)
    set_description("Build tests using the header only version")
option_end()

option("build_bench")
    set_default(false)
    set_showmenu(true)
    set_description("Build benchmarks")
option_end()

option("sanitize_address")
    set_default(false)
    set_showmenu(true)
    set_description("Enable address sanitizer in tests")
option_end()

option("sanitize_thread")
    set_default(false)
    set_showmenu(true)
    set_description("Enable thread sanitizer in tests")
option_end()

option("build_warnings")
    set_default(false)
    set_showmenu(true)
    set_description("Enable compiler warnings")
option_end()

option("use_std_format")
    set_default(false)
    set_showmenu(true)
    set_description("Use std::format instead of fmt library")
option_end()

option("fmt_external")
    set_default(false)
    set_showmenu(true)
    set_description("Use external fmt library instead of bundled")
option_end()

option("fmt_external_ho")
    set_default(false)
    set_showmenu(true)
    set_description("Use external fmt header-only library instead of bundled")
option_end()

option("no_exceptions")
    set_default(false)
    set_showmenu(true)
    set_description("Compile with -fno-exceptions")
option_end()

option("no_tz_offset")
    set_default(false)
    set_showmenu(true)
    set_description("Omit %z timezone offset")
option_end()

option("wchar_support")
    set_default(false)
    set_showmenu(true)
    set_description("Support wchar api (Windows only)")
option_end()

option("wchar_filenames")
    set_default(false)
    set_showmenu(true)
    set_description("Support wchar filenames (Windows only)")
option_end()

option("wchar_console")
    set_default(false)
    set_showmenu(true)
    set_description("Support wchar output to console (Windows only)")
option_end()

option("clock_coarse")
    set_default(false)
    set_showmenu(true)
    set_description("Use CLOCK_REALTIME_COARSE instead of the regular clock (Linux only)")
option_end()

option("prevent_child_fd")
    set_default(false)
    set_showmenu(true)
    set_description("Prevent from child processes to inherit log file descriptors")
option_end()

option("no_thread_id")
    set_default(false)
    set_showmenu(true)
    set_description("Prevent spdlog from querying the thread id on each log call")
option_end()

option("no_tls")
    set_default(false)
    set_showmenu(true)
    set_description("Prevent spdlog from using thread local storage")
option_end()

option("no_atomic_levels")
    set_default(false)
    set_showmenu(true)
    set_description("Prevent spdlog from using std::atomic log levels")
option_end()

option("disable_default_logger")
    set_default(false)
    set_showmenu(true)
    set_description("Disable default logger creation")
option_end()

option("fwrite_unlocked")
    set_default(true)
    set_showmenu(true)
    set_description("Use the unlocked variant of fwrite")
option_end()

-- 多进程共享内存支持选项
option("enable_multiprocess")
    set_default(false)
    set_showmenu(true)
    set_description("Enable multiprocess shared memory support")
option_end()

-- 辅助函数：应用警告配置
function apply_warnings()
    if has_config("build_warnings") then
        if is_plat("windows") then
            add_cxflags("/W3", {force = true})
        else
            add_cxflags("-Wall", "-Wextra", "-Wconversion", "-pedantic", "-Werror", "-Wfatal-errors", {force = true})
        end
    end
end

-- 辅助函数：应用地址消毒器
function apply_addr_sanitizer()
    if has_config("sanitize_address") then
        add_cxflags("-fsanitize=address,undefined", "-fno-sanitize=signed-integer-overflow", 
                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", {force = true})
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
end

-- 辅助函数：应用线程消毒器
function apply_thread_sanitizer()
    if has_config("sanitize_thread") then
        add_cxflags("-fsanitize=thread", "-fno-omit-frame-pointer", {force = true})
        add_ldflags("-fsanitize=thread", {force = true})
    end
end

-- 辅助函数：应用通用spdlog配置
function apply_spdlog_config()
    -- 添加头文件目录
    add_includedirs("include", {public = true})
    
    -- 添加线程库
    add_syslinks("pthread")
    
    -- Android平台添加log库
    if is_plat("android") then
        add_syslinks("log")
    end
    
    -- 添加编译定义
    add_defines("SPDLOG_COMPILED_LIB", {public = true})
    
    -- 处理std::format选项
    if has_config("use_std_format") then
        set_languages("cxx20")
        add_defines("SPDLOG_USE_STD_FORMAT", {public = true})
    end
    
    -- 处理外部fmt库
    if has_config("fmt_external") or has_config("fmt_external_ho") then
        add_defines("SPDLOG_FMT_EXTERNAL", {public = true})
        add_packages("fmt")
    end
    
    -- 处理各种配置选项
    if has_config("wchar_support") then
        add_defines("SPDLOG_WCHAR_TO_UTF8_SUPPORT", {public = true})
    end
    
    if has_config("wchar_console") then
        add_defines("SPDLOG_UTF8_TO_WCHAR_CONSOLE", {public = true})
    end
    
    if has_config("wchar_filenames") then
        add_defines("SPDLOG_WCHAR_FILENAMES", {public = true})
    end
    
    if has_config("no_exceptions") then
        add_defines("SPDLOG_NO_EXCEPTIONS", {public = true})
        if not has_config("fmt_external") and not has_config("fmt_external_ho") then
            add_defines("FMT_USE_EXCEPTIONS=0", {public = true})
        end
        if is_plat("windows") then
            add_cxflags("/EHs-c-", {force = true})
            add_defines("_HAS_EXCEPTIONS=0")
        else
            add_cxflags("-fno-exceptions", {force = true})
        end
    end
    
    if has_config("clock_coarse") then
        add_defines("SPDLOG_CLOCK_COARSE", {public = true})
    end
    
    if has_config("prevent_child_fd") then
        add_defines("SPDLOG_PREVENT_CHILD_FD", {public = true})
    end
    
    if has_config("no_thread_id") then
        add_defines("SPDLOG_NO_THREAD_ID", {public = true})
    end
    
    if has_config("no_tls") then
        add_defines("SPDLOG_NO_TLS", {public = true})
    end
    
    if has_config("no_atomic_levels") then
        add_defines("SPDLOG_NO_ATOMIC_LEVELS", {public = true})
    end
    
    if has_config("disable_default_logger") then
        add_defines("SPDLOG_DISABLE_DEFAULT_LOGGER", {public = true})
    end
    
    if has_config("no_tz_offset") then
        add_defines("SPDLOG_NO_TZ_OFFSET", {public = true})
    end
    
    -- 多进程共享内存支持
    if has_config("enable_multiprocess") then
        add_defines("SPDLOG_ENABLE_MULTIPROCESS", {public = true})
    end
    
    -- 处理fwrite_unlocked (需要检查系统是否支持)
    if has_config("fwrite_unlocked") then
        -- macOS不支持fwrite_unlocked，跳过此定义
        if not is_plat("macosx") then
            add_defines("SPDLOG_FWRITE_UNLOCKED")
        end
    end
    
    -- MSVC特定选项
    if is_plat("windows") and is_mode("debug") then
        set_basename("spdlogd")
    end
    
    if is_plat("windows") then
        add_cxflags("/Zc:__cplusplus", {force = true})
        add_cxflags("/utf-8", {force = true})
    end
    
    -- 启用警告
    apply_warnings()
    
    -- 启用消毒器
    if has_config("sanitize_address") then
        apply_addr_sanitizer()
    elseif has_config("sanitize_thread") then
        apply_thread_sanitizer()
    end
end

-- 主库目标（编译版本）
target("spdlog")
    set_kind(has_config("build_shared") and "shared" or "static")
    
    -- 添加源文件
    add_files("src/spdlog.cpp")
    add_files("src/stdout_sinks.cpp")
    add_files("src/color_sinks.cpp")
    add_files("src/file_sinks.cpp")
    add_files("src/async.cpp")
    add_files("src/cfg.cpp")
    
    -- 如果不使用std::format且不使用外部fmt，添加bundled fmt
    if not has_config("use_std_format") and not has_config("fmt_external") and not has_config("fmt_external_ho") then
        add_files("src/bundled_fmtlib_format.cpp")
    end
    
    -- 如果启用多进程支持，添加多进程源文件
    if has_config("enable_multiprocess") then
        add_files("src/multiprocess/shared_memory_manager.cpp")
        add_files("src/multiprocess/lock_free_ring_buffer.cpp")
        add_files("src/multiprocess/shared_memory_producer_sink.cpp")
        add_files("src/multiprocess/shared_memory_consumer_sink.cpp")
        add_files("src/multiprocess/config.cpp")
        add_files("src/multiprocess/mode.cpp")
    end
    
    -- 共享库特定配置
    if has_config("build_shared") then
        add_defines("SPDLOG_SHARED_LIB", {public = true})
        if not has_config("use_std_format") and not has_config("fmt_external") and not has_config("fmt_external_ho") then
            add_defines("FMT_LIB_EXPORT", "FMT_SHARED", {public = true})
        end
    end
    
    -- 应用通用配置
    apply_spdlog_config()
    
    -- PIC支持
    if has_config("build_pic") then
        set_policy("build.c++.pic", true)
    end
    
    -- 预编译头支持
    if has_config("enable_pch") then
        set_pcxxheader("include/spdlog/spdlog.h")
    end
target_end()

-- 头文件库目标
target("spdlog_header_only")
    set_kind("headeronly")
    add_headerfiles("include/(**.h)")
    add_includedirs("include", {public = true})
    add_syslinks("pthread")
    
    if is_plat("android") then
        add_syslinks("log")
    end
    
    -- 添加与编译版本相同的定义（除了SPDLOG_COMPILED_LIB）
    if has_config("use_std_format") then
        set_languages("cxx20")
        add_defines("SPDLOG_USE_STD_FORMAT", {public = true})
    end
    
    if has_config("fmt_external") or has_config("fmt_external_ho") then
        add_defines("SPDLOG_FMT_EXTERNAL", {public = true})
        add_packages("fmt")
    end
    
    if has_config("wchar_support") then
        add_defines("SPDLOG_WCHAR_TO_UTF8_SUPPORT", {public = true})
    end
    
    if has_config("wchar_console") then
        add_defines("SPDLOG_UTF8_TO_WCHAR_CONSOLE", {public = true})
    end
    
    if has_config("wchar_filenames") then
        add_defines("SPDLOG_WCHAR_FILENAMES", {public = true})
    end
    
    if has_config("no_exceptions") then
        add_defines("SPDLOG_NO_EXCEPTIONS", {public = true})
    end
    
    if has_config("clock_coarse") then
        add_defines("SPDLOG_CLOCK_COARSE", {public = true})
    end
    
    if has_config("prevent_child_fd") then
        add_defines("SPDLOG_PREVENT_CHILD_FD", {public = true})
    end
    
    if has_config("no_thread_id") then
        add_defines("SPDLOG_NO_THREAD_ID", {public = true})
    end
    
    if has_config("no_tls") then
        add_defines("SPDLOG_NO_TLS", {public = true})
    end
    
    if has_config("no_atomic_levels") then
        add_defines("SPDLOG_NO_ATOMIC_LEVELS", {public = true})
    end
    
    if has_config("disable_default_logger") then
        add_defines("SPDLOG_DISABLE_DEFAULT_LOGGER", {public = true})
    end
    
    if has_config("no_tz_offset") then
        add_defines("SPDLOG_NO_TZ_OFFSET", {public = true})
    end
    
    if has_config("enable_multiprocess") then
        add_defines("SPDLOG_ENABLE_MULTIPROCESS", {public = true})
    end
    
    if has_config("fwrite_unlocked") then
        add_defines("SPDLOG_FWRITE_UNLOCKED", {public = true})
    end
    
    if is_plat("windows") then
        add_cxflags("/Zc:__cplusplus", {force = true})
        add_cxflags("/utf-8", {force = true})
    end
target_end()

-- 示例程序
if has_config("build_example") then
    target("example")
        set_kind("binary")
        add_files("example/example.cpp")
        add_deps("spdlog")
        apply_warnings()
        
        if is_plat("mingw") then
            add_syslinks("ws2_32")
        end
    target_end()
    
    -- 多进程示例程序
    if has_config("enable_multiprocess") then
        target("example_mp1_consumer")
            set_kind("binary")
            add_files("example/example_mp1_consumer.cpp")
            add_deps("spdlog")
            add_defines("SPDLOG_ENABLE_MULTIPROCESS")
        target_end()
        
        target("example_mp1_producer")
            set_kind("binary")
            add_files("example/example_mp1_producer.cpp")
            add_deps("spdlog")
            add_defines("SPDLOG_ENABLE_MULTIPROCESS")
        target_end()
        
        target("example_mp2")
            set_kind("binary")
            add_files("example/example_mp2.cpp")
            add_deps("spdlog")
            add_defines("SPDLOG_ENABLE_MULTIPROCESS")
        target_end()
        
        target("example_mp3")
            set_kind("binary")
            add_files("example/example_mp3.cpp")
            add_deps("spdlog")
            add_defines("SPDLOG_ENABLE_MULTIPROCESS")
        target_end()
    end
end

if has_config("build_example_ho") then
    target("example_header_only")
        set_kind("binary")
        add_files("example/example.cpp")
        add_deps("spdlog_header_only")
        apply_warnings()
    target_end()
end

-- 测试程序
if has_config("build_tests") or has_config("build_tests_ho") then
    add_requires("catch2 3.x")
    
    if has_config("build_tests") then
        target("spdlog-utests")
            set_kind("binary")
            add_files("tests/*.cpp")
            add_deps("spdlog")
            add_packages("catch2")
            apply_warnings()
            
            if has_config("sanitize_address") then
                apply_addr_sanitizer()
            elseif has_config("sanitize_thread") then
                apply_thread_sanitizer()
            end
        target_end()
    end
    
    if has_config("build_tests_ho") then
        target("spdlog-utests-ho")
            set_kind("binary")
            add_files("tests/*.cpp")
            add_deps("spdlog_header_only")
            add_packages("catch2")
            apply_warnings()
            
            if has_config("sanitize_address") then
                apply_addr_sanitizer()
            elseif has_config("sanitize_thread") then
                apply_thread_sanitizer()
            end
        target_end()
    end
end

-- 基准测试程序
if has_config("build_bench") then
    add_requires("benchmark")
    
    target("bench")
        set_kind("binary")
        add_files("bench/bench.cpp")
        add_deps("spdlog")
        apply_warnings()
    target_end()
    
    target("async_bench")
        set_kind("binary")
        add_files("bench/async_bench.cpp")
        add_deps("spdlog")
    target_end()
    
    target("latency")
        set_kind("binary")
        add_files("bench/latency.cpp")
        add_deps("spdlog")
        add_packages("benchmark")
    target_end()
    
    target("formatter-bench")
        set_kind("binary")
        add_files("bench/formatter-bench.cpp")
        add_deps("spdlog")
        add_packages("benchmark")
    target_end()
    
    -- 多进程共享内存日志性能基准测试
    if has_config("enable_multiprocess") then
        target("multiprocess_bench")
            set_kind("binary")
            add_files("bench/multiprocess_bench.cpp")
            add_deps("spdlog")
            add_defines("SPDLOG_ENABLE_MULTIPROCESS")
        target_end()
    end
end
