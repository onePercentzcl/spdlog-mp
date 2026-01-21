add_rules("mode.debug", "mode.release")

set_languages("cxx17")

-- 使用本地仓库中的 spdlog-mp 包
add_repositories("local-repo ~/xmake-repo")
add_requires("spdlog-mp v1.0.3", {configs = {enable_multiprocess = true}})

target("test_release")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("spdlog-mp")
target_end()
