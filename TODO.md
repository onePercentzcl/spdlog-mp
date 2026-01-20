# TODO List

## 待完成工作

### 1. 非 Fork 场景下 eventfd/kqueue 验证问题

**问题描述：**
在非 fork 场景下（独立进程），生产者进程从共享内存读取的 eventfd/kqueue 文件描述符在当前进程中是无效的。生产者会尝试写入该 fd，但会失败。虽然系统会退化到轮询模式（消息不会丢失），但会产生无效的系统调用。

**当前行为：**
- 生产者从共享内存读取 `eventfd_` 值
- 在非 fork 进程中，该 fd 值无效
- `write(eventfd_)` 或 `kevent(eventfd_)` 会失败
- 代码忽略返回值，继续执行
- 消费者通过轮询（poll_interval=10ms）检测新消息

**建议改进：**
1. 在生产者初始化时验证 eventfd/kqueue 是否可用
2. 尝试一次测试性写入/操作，检查返回值
3. 如果失败，将 `eventfd_` 设置为 -1
4. 添加 debug 日志提示已切换到轮询模式

**影响：**
- 功能正常（消息不会丢失）
- 性能影响较小（每次 commit 会有一次失败的系统调用）
- 用户体验：debug 模式下会看到误导性的 "sending eventfd notification" 日志

**优先级：** 中等

**相关文件：**
- `src/multiprocess/lock_free_ring_buffer.cpp` - 构造函数和 notify_consumer()
- `include/spdlog/multiprocess/lock_free_ring_buffer.h`

---

## 已完成工作

### ✅ 修复 poll_duration 配置不生效问题
- 将硬编码的 30 秒改为可配置参数
- 默认值：1 秒
- 已在 Linux 和 macOS 上测试通过

### ✅ 修复 Linux fork 子进程共享内存附加失败
- 修改 attach_internal() 优先使用 shm_open by name
- 添加必要的头文件

### ✅ 添加 Linux 编译所需的 #include <mutex>
- 修复 GCC 编译错误

### ✅ 添加 POLLING 状态优化的 debug 日志
- 显示 POLLING 持续时间
- 显示剩余时间
- 显示状态转换

### ✅ 创建测试程序
- test_eventfd.cpp - 验证 fork 场景 eventfd 工作
- test_polling_optimization.cpp - 验证 POLLING 优化
- test_poll_duration_config.cpp - 验证配置生效
- test_polling_vs_notification.cpp - 对比轮询和通知模式
