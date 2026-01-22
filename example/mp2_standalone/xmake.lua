-- xmake.lua for spdlog-mp example_mp2
-- 从远程仓库下载 spdlog-mp 库

set_project("example_mp2")
set_version("0.0.1")

-- 设置 C++17 标准
set_languages("cxx17")

-- 配置编译模式
add_rules("mode.debug", "mode.release")

-- 设置默认为 release 模式
set_defaultmode("release")

-- 添加 spdlog-mp 包（从自定义仓库）
add_repositories("xmake-repo- https://github.com/onePercentzcl/xmake-repo-")
add_requires("spdlog-mp v0.0.1", {configs = {enable_multiprocess = true}})

-- example_mp2 程序
target("example_mp2")
    set_kind("binary")
    add_files("example_mp2.cpp")
    add_packages("spdlog-mp")
    add_syslinks("pthread", "rt")
target_end()
