# 嵌入式 Linux / C++ 岗位简历版本

## 证据边界

此版本强调用户态 C++ 工程能力：RAII、`unique_ptr/shared_ptr`、线程、原子停止标志、条件变量、有界队列和错误传播。当前工作树未完成 C++ 编译/CTest，不能把静态源码能力写成已通过运行压测。

## 项目名称与技术栈

**双路 Camera 实时处理的 C++ 并发与资源生命周期设计**
技术栈：C++17、`std::thread`、`std::atomic`、`mutex`、`condition_variable`、`unique_ptr/shared_ptr`、有界生产者消费者队列、V4L2 ioctl/mmap、错误回收。

## 80～120 字项目描述（当前已验证版）

在 RK3588 双路 Camera 用户态程序中，按 Capture、AI、Compose/Encode 边界组织线程和资源所有权：相机帧通过 `CameraFrame`/`FrameLease` 跨队列传递，队列满时丢旧并统计，停止时先关闭队列再 join，最后由 lease 触发 QBUF。代码级实现了异常返回与单次 run 约束，但编译、线程压力和硬件 SDK 语义仍待验证。

## 四条核心技术工作

1. `include/bounded_frame_queue.h` 提供容量受限队列、`push_drop_oldest`、关闭唤醒和 `pop`；双路 pair 队列深度 4、每路 AI 队列深度 2，避免无限积压。
2. `include/camera_frame.h:76-167` 通过 `shared_ptr<CameraState>` 保证 mmap/fd 生命周期，`FrameLease` 析构时最后一个引用执行 QBUF；公开 owner/lease 仍存在可拆分风险，已列为后续改进。
3. `DualCameraPipeline::run`（`lib/dual_camera_pipeline.cpp:383-451`）创建 2 个 capture、最多 2 个 AI 和 1 个 encode 线程；`request_stop` 关闭队列并唤醒等待者，析构路径 join 后停止相机。
4. 通过 `error_mutex`、`stats_mutex`、原子 stop 和明确返回码传播初始化、V4L2、MPP、sink 错误；同步 RKNN 调用不可取消，队列失败后的 drain 也需要进一步加强。

## 面试追问与回答边界

| 追问 | 基于证据的回答 |
|---|---|
| 是不是零拷贝？ | 不是。为了避免异步硬件持有相机 mmap，AI/编码前先 copy 到自有缓冲；DMA-BUF import/fence 还没有板端证据。 |
| 如何保证 QBUF 不早于消费者使用？ | 每个 `CameraFrame` 带 lease，队列和消费者都持有副本，最后一个 lease 释放才 QBUF；停止前等待 in-flight lease 归零。 |
| 队列满了怎么办？ | `BoundedFrameQueue` 丢最旧并增加统计，适合实时性优先的 AI 分支；录像/编码是否允许丢帧需按业务策略验证。 |
| stop 一定有界吗？ | 不能完全保证。RKNN `interf` 是同步调用，若 SDK 卡住，join 可能等待；这是当前明确的 P1 风险。 |

## 目标完成后版本（仅草稿）

只有完成编译、TSAN/ASAN、慢消费者、设备断开和 MPP timeout 注入后，才可写入“无泄漏、无死锁、稳定运行”等结论。当前只可写设计和源码证据。

## 对应证据文件

`include/bounded_frame_queue.h`、`include/camera_frame.h`、`lib/dual_camera_pipeline.cpp:178-218,238-451`、`include/dual_camera_pipeline.h`、`tests/camera_lifecycle_test.cpp`、`tests/frame_pairer_test.cpp`、`docs/PIPELINE_AND_OWNERSHIP.md`。

