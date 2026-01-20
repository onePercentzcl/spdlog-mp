# CMakeLists.txt for spdlog tools
cmake_minimum_required(VERSION 3.11)

# 孤立共享内存清理工具
if(SPDLOG_ENABLE_MULTIPROCESS)
    add_executable(spdlog-shm-cleanup shm_cleanup.cpp)
    
    # 设置C++标准
    set_target_properties(spdlog-shm-cleanup PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
    
    # 链接必要的库
    if(UNIX AND NOT APPLE)
        # Linux需要链接rt库
        target_link_libraries(spdlog-shm-cleanup PRIVATE rt)
    endif()
    
    # 安装目标
    install(TARGETS spdlog-shm-cleanup
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
