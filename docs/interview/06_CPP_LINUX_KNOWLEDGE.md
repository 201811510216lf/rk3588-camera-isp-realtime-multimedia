# C++ / Linux 系统编程知识体系

## 1. RAII 与资源所有权

RAII 的核心是让对象生命周期绑定资源释放。项目中 `V4L2IspCamera`、`MppEncoder`、`DualCameraPipeline` 使用析构函数和 `unique_ptr/shared_ptr` 管理对象；`FrameLease` 析构时归还 V4L2 slot（`include/camera_frame.h:144-170`）。

但 `CameraFrame` 的 `owner` 与 `lease` 是两个公开 shared_ptr（`:172-179`），接口约束仍需谨慎。面试回答应说明：调用者不能自行 close fd/mmap，最后一个 lease 才能 QBUF。

## 2. shared_ptr / unique_ptr

`unique_ptr` 表示单一拥有者，例如 `models[2]`、`FfmpegPipe`；`shared_ptr` 适合跨线程共享 CameraFrame lease 和 encoded packet bytes。shared_ptr 只管理 C++ 对象生命周期，不自动证明硬件 DMA 已经完成。

项目关键点：`EncodedPacket` 用 `shared_ptr<const vector<uint8_t>>` 让多个 sink 共用 packet；`FrameLease` 保存 owner 防止 camera object 先销毁。

## 3. mutex、condition_variable、atomic

`BufferLeaseState` 用 mutex + condition_variable 等待 in-flight 归零；`BoundedFrameQueue::pop` 用 condition_variable 等待数据或 close；pipeline 用 `atomic<bool> stop` 传播停止请求。

面试中应区分：

- atomic 适合简单 stop flag；
- mutex 保护队列、统计、AI snapshot；
- condition_variable 需要谓词，防止虚假唤醒；
- 仍要审查 lock 顺序和线程退出边界。

## 4. 生产者消费者与背压

Capture 是生产者，pair/AI/encode 是消费者。`BoundedFrameQueue` 容量固定，满时 drop-oldest；这避免无限内存增长，但录像主链路是否允许丢帧必须由业务决定。

当前 dual pipeline 有 pair queue、两个 AI queue 和 encode thread；`lib/dual_camera_pipeline.cpp:238-284` 采集并推入 pairer，`:366-379` 编码。sink 写入仍在 encode thread 同步执行，因此慢网络会反向阻塞编码。

## 5. thread 与退出

`main.cpp:19-101` 注册 SIGINT/SIGTERM，构造 `DualCameraPipeline` 并调用 `run()`；析构中 request_stop、关闭队列、join capture/AI/encode、停止 camera（`dual_camera_pipeline.cpp:383-385`）。

正确退出顺序：停止接收 → 关闭队列 → 消费者观察 close 并退出 → join → 等 lease 归零 → STREAMOFF → 释放 mmap/fd。当前 AI 模型调用不可取消，属于已知 P1 风险。

## 6. poll、ioctl、mmap

`V4L2IspCamera::dequeue` 使用 `poll` 等待设备，再 `VIDIOC_DQBUF`；`open` 使用 `ioctl` 查询 capability/format/buffer；`QUERYBUF` 的 offset/length 用 `mmap` 映射。

系统调用失败必须保留 errno、设备名、ioctl 名和协商参数。当前 `lib/v4l2_isp_camera.cpp:223-499,506-697` 已有错误字符串和 payload 边界校验，但 QUERYBUF plane count 一致性仍需收紧。

## 7. 数据竞争与锁边界

FramePairer 用 mutex 保护两个队列和统计；AI snapshot 用 `ai_mutex` 写入/读取；events/sync_diag 有独立 mutex。任何跨线程字段都要问“谁写、谁读、什么锁/原子保证”。

潜在面试题：为什么不能在没有锁的情况下读 `streaming`？因为 stop/capture 并发时会形成数据竞态；当前接口主要按单线程 capture 使用，但并发契约要明确。

## 8. 异常与错误传播

线程函数中的异常如果没有边界会调用 `std::terminate`，绕过 failure 和清理。当前主要路径依赖返回值和 `fail()`，但 RGA/OpenCV/RKNN 构造和调用边界仍缺少统一异常保护。

错误传播应满足：底层失败 → 设置 pipeline failure → 唤醒/关闭队列 → join → 资源清理 → 主程序非零退出。sink false、events flush 失败和 ffmpeg 退出当前没有完整满足该链路。

## 9. C++ 工程面试表达

可以讲：

- C++17、RAII、智能指针和显式 ownership；
- 有界队列和 drop policy；
- timestamp/sequence 关联；
- V4L2 lease 回收；
- 单编码包多目的分发。

必须主动承认：CMake/C++ 构建在当前主机未执行，RK3588/BSP/MPP/RKNN/RTMP/MP4 也没有板端证据。

## 10. 扩展学习

epoll、io_uring、eventfd、signalfd、线程命名/调度、lock-free queue、TSAN/ASAN、perf、ftrace、dmabuf-heaps 和 Linux driver waitqueue 属于岗位扩展知识，不代表当前工程已使用。
