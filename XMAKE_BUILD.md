# xmake构建指南

本文档介绍如何使用xmake构建spdlog项目。

## 前置要求

- xmake >= 2.5.0
- C++11或更高版本的编译器

## 安装xmake

### macOS
```bash
brew install xmake
```

### Linux
```bash
curl -fsSL https://xmake.io/shget.text | bash
```

### Windows
```bash
Invoke-Expression (Invoke-Webrequest 'https://xmake.io/psget.text' -UseBasicParsing).Content
```

更多安装方式请参考：https://xmake.io/#/guide/installation

## 基本用法

### 配置项目
```bash
# 使用默认配置
xmake f -c

# 指定构建模式
xmake f -c -m release  # 或 -m debug
```

### 构建项目
```bash
# 构建所有目标
xmake

# 构建特定目标
xmake build spdlog              # 编译版本库
xmake build spdlog_header_only  # 头文件库
```

### 清理构建
```bash
xmake clean
```

## 配置选项

### 库类型选项
```bash
# 构建共享库（默认为静态库）
xmake f -c --build_shared=y

# 启用预编译头
xmake f -c --enable_pch=y

# 构建位置无关代码
xmake f -c --build_pic=y
```

### 示例和测试选项
```bash
# 构建示例程序
xmake f -c --build_example=y
xmake build example

# 构建头文件版本示例
xmake f -c --build_example_ho=y
xmake build example_header_only

# 构建测试
xmake f -c --build_tests=y
xmake build spdlog-utests

# 构建头文件版本测试
xmake f -c --build_tests_ho=y
xmake build spdlog-utests-ho

# 构建基准测试
xmake f -c --build_bench=y
xmake build bench async_bench latency formatter-bench
```

### 多进程共享内存支持（新功能）
```bash
# 启用多进程共享内存支持
xmake f -c --enable_multiprocess=y
xmake build spdlog
```

### 格式化库选项
```bash
# 使用std::format替代fmt库（需要C++20）
xmake f -c --use_std_format=y

# 使用外部fmt库
xmake f -c --fmt_external=y

# 使用外部fmt头文件库
xmake f -c --fmt_external_ho=y
```

### 编译器和调试选项
```bash
# 启用编译器警告
xmake f -c --build_warnings=y

# 启用地址消毒器（Address Sanitizer）
xmake f -c --sanitize_address=y

# 启用线程消毒器（Thread Sanitizer）
xmake f -c --sanitize_thread=y
```

### 功能选项
```bash
# 禁用异常
xmake f -c --no_exceptions=y

# 禁用时区偏移
xmake f -c --no_tz_offset=y

# 禁用线程ID查询
xmake f -c --no_thread_id=y

# 禁用线程局部存储
xmake f -c --no_tls=y

# 禁用原子日志级别
xmake f -c --no_atomic_levels=y

# 禁用默认logger
xmake f -c --disable_default_logger=y

# 防止子进程继承文件描述符
xmake f -c --prevent_child_fd=y

# 禁用fwrite_unlocked
xmake f -c --fwrite_unlocked=n
```

### Windows特定选项
```bash
# 启用wchar支持（仅Windows）
xmake f -c --wchar_support=y

# 启用wchar文件名支持（仅Windows）
xmake f -c --wchar_filenames=y

# 启用wchar控制台输出支持（仅Windows）
xmake f -c --wchar_console=y
```

### Linux特定选项
```bash
# 使用CLOCK_REALTIME_COARSE（仅Linux）
xmake f -c --clock_coarse=y
```

## 完整示例

### 构建发布版本库
```bash
xmake f -c -m release
xmake build spdlog
```

### 构建调试版本并启用警告
```bash
xmake f -c -m debug --build_warnings=y
xmake build spdlog
```

### 构建共享库和示例
```bash
xmake f -c --build_shared=y --build_example=y
xmake build spdlog example
```

### 构建启用多进程支持的版本
```bash
xmake f -c --enable_multiprocess=y
xmake build spdlog
```

### 构建测试并启用地址消毒器
```bash
xmake f -c --build_tests=y --sanitize_address=y
xmake build spdlog-utests
xmake run spdlog-utests
```

## 与CMake的对比

xmake配置与CMake选项的对应关系：

| xmake选项 | CMake选项 |
|-----------|-----------|
| `--build_shared=y` | `-DSPDLOG_BUILD_SHARED=ON` |
| `--enable_pch=y` | `-DSPDLOG_ENABLE_PCH=ON` |
| `--build_pic=y` | `-DSPDLOG_BUILD_PIC=ON` |
| `--build_example=y` | `-DSPDLOG_BUILD_EXAMPLE=ON` |
| `--build_tests=y` | `-DSPDLOG_BUILD_TESTS=ON` |
| `--build_bench=y` | `-DSPDLOG_BUILD_BENCH=ON` |
| `--use_std_format=y` | `-DSPDLOG_USE_STD_FORMAT=ON` |
| `--fmt_external=y` | `-DSPDLOG_FMT_EXTERNAL=ON` |
| `--no_exceptions=y` | `-DSPDLOG_NO_EXCEPTIONS=ON` |
| `--enable_multiprocess=y` | `-DSPDLOG_ENABLE_MULTIPROCESS=ON` |

## 查看所有选项

```bash
xmake f --help
```

## 故障排除

### 问题：找不到xmake命令
**解决方案**：确保xmake已正确安装并添加到PATH环境变量中。

### 问题：编译错误
**解决方案**：
1. 清理构建：`xmake clean -a`
2. 重新配置：`xmake f -c`
3. 重新构建：`xmake build`

### 问题：链接错误
**解决方案**：确保所有依赖项都已正确安装。对于外部fmt库，使用：
```bash
xmake repo --add-repo xmake-repo https://github.com/xmake-io/xmake-repo.git
xmake require -y fmt
```

## 更多信息

- xmake官方文档：https://xmake.io/#/guide/introduction
- spdlog官方文档：https://github.com/gabime/spdlog
