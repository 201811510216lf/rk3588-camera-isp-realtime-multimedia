# 三轮模拟压力面试

以下回答基于当前工作树的双路 pipeline。每题先给面试官想考察什么，再给容易被质疑的回答和基于真实项目的回答。源码实现不等于板端验证。

## 第一轮：基础面

### 1. Sensor 到 V4L2 NV12 的链路
- **考察：** Camera 分层和驱动边界。
- **容易被质疑：** “我实现了 RKISP 和 Sensor 驱动。”
- **合理回答：** Sensor 通过 MIPI CSI-2 输出 RAW，经 CSI/RKCIF 接收、RKISP 处理后，由 V4L2 video node 提供 NV12/NV12M。当前仓库只实现 V4L2IspCamera 用户态打开、格式协商和 buffer 生命周期，Sensor/DTS/ISP 驱动来自目标 BSP，仓库没有对应源码或板端日志。
- **深入：** lib/v4l2_isp_camera.cpp:203-483 的 ioctl 顺序证明的是 video node 接入，不是 ISP 算法实现。

### 2. NV12 与 NV12M
- **考察：** 多平面布局和越界风险。
- **合理回答：** NV12 可为一块连续 plane，NV12M 的 Y/UV 地址独立；读取必须使用 plane 数、offset、bytesused 和 stride。copy_nv12 按行复制，避免把 padding 当像素。异常 plane 数仍需更严格拒绝。
- **深入：** 说明 S_FMT/QUERYBUF 的协商值和驱动回读。

### 3. 时间戳接近是否等于硬件同步？
- **考察：** 时间基和曝光语义。
- **合理回答：** timestamp 可能来自 SOF、EOF 或采集时刻，两个 Sensor 的曝光起点/时钟也可能不同。FramePairer 只在 clock domain 相同且 delta 在 tolerance 内配帧；hardware 模式没有验证 FSIN/Trigger backend 时 fail-fast。
- **深入：** 需要 timestamp flags、SOF 日志和示波器/触发线证据。

### 4. V4L2 buffer 何时归还？
- **考察：** 所有权和 QBUF 时机。
- **合理回答：** CameraFrame 携带 FrameLease，Capture、pair queue、AI/compose 都可能持有副本；最后一个 lease 释放时才 QBUF。停止时先 stop accepting、等待 in-flight，再 STREAMOFF/munmap/close。
- **深入：** include/camera_frame.h:76-167；公开 owner/lease 仍有可拆分风险。

### 5. C++ 线程如何停止？
- **考察：** join、唤醒和取消边界。
- **合理回答：** request_stop 设置 atomic stop 并关闭有界队列；capture 的 poll 有超时，线程随后退出并 join。同步 rknn_lite::interf 和 ffmpeg fwrite 不能被 atomic 强制中断，stop 仍可能等待。
- **深入：** 需要 SDK timeout、独立 worker 或进程隔离提供更强的有界退出。

### 6. 为什么选 RGA？
- **考察：** 硬件加速和 fallback。
- **合理回答：** RGA 用于 NV12↔BGR 和 resize，失败时 OpenCV fallback；当前先把 V4L2 buffer copy 到自有 packed NV12，保证 ownership 可证明。没有板端 benchmark，不能宣称性能收益。
- **深入：** 讲 stride、format、DMA-BUF import/cache/fence 仍需验证。

### 7. MPP、RGA、RKNN 分别承担什么？
- **考察：** 多媒体硬件模块边界。
- **合理回答：** RGA 做颜色/缩放/拼接辅助，RKNN 做 NPU 推理，MPP 做 H.264 编码。CMake 只在 RK demo target 链接 vendor SDK；主机静态检查不能证明 SDK 可运行。

### 8. shared_ptr 解决什么？
- **考察：** 所有权而非万能并发。
- **合理回答：** CameraState 和 EncodedPacket payload 需要跨线程共享生命周期，shared_ptr 避免提前释放；共享对象内部状态仍需 mutex，shared_ptr 不自动解决数据竞争。

### 9. poll 超时算丢帧吗？
- **合理回答：** 超时是等待窗口无帧，capture_loop 会调用 pairer.tick 并继续；sequence 跳变、DQBUF 错误和 pair drop 分别统计。不能把所有 timeout 等同于丢帧。

### 10. 当前验证状态？
- **合理回答：** tests/validate_project.py、Python compileall 和静态检查通过；无 CMake/C++ 编译器，CTest 未执行；无 RK3588/BSP/Sensor/模型/RTMP 服务器，TARGET_BUILT、BOARD_TESTED、MEASURED 均未测。

## 第二轮：项目深挖

### 1. 从 main.cpp 讲执行顺序
- **合理回答：** main 解析 AppOptions，填充 DualCameraPipelineConfig，注册 SIGINT，调用 run，打印 stats 和返回码。run 检查单次使用和 hardware sync，配置 FramePairer，打开/启动两路 Camera，检查尺寸/fps，建立 sink、MPP header、模型和线程，最后 join、release、stop。
- **追问：** CLI npu_core0/1 是否真正生效？pipeline 构造 rknn_lite 时固定 n=0，参数只打印 warning；wrapper 有 core-mask API，二者存在实现缺口。

### 2. FramePairer 如何匹配？
- **合理回答：** 两路维护有界单调队列，要求 clock domain 已知且相同；在 tolerance 内选择 delta 最小组合，过期帧按 policy 丢弃，记录 pair/drop/P50/P95/P99。
- **追问：** hold-last 是否计 matched？不计入新鲜 matched，标 stale。

### 3. 队列深度为什么是 4/2？
- **合理回答：** 是实时性优先的有限缓存，避免慢消费者长期占用 V4L2 slots；不是实测最优值，需在目标板用 drop/latency 数据调参。

### 4. 两路 RKNN 怎么运行？
- **合理回答：** 每路 AI queue 和独立 rknn_lite context，AI loop copy NV12、RGA/OpenCV 转 BGR、调用 interf，生成带 camera_id/sequence/timestamp 的 snapshot。模型缺失时可降级，ai_required 时失败。
- **追问：** 是否两个 NPU 核？当前构造固定 n=0，不能这样宣称。

### 5. OSD 如何避免画错帧？
- **合理回答：** snapshot 保存 sequence/timestamp；compose 对 strict 模式等待对应 sequence，并要求 snapshot timestamp<=画面 timestamp；不满足则 AI_WAIT，不画框。

### 6. 为什么统一拼接后一个 MPP？
- **合理回答：** 输出是一张合成画面，单路编码减少重复编码和码率管理；没有双编码器实测对比。

### 7. RTMP 和 MP4 怎样复用？
- **合理回答：** EncodedPacketDistributor 以 shared payload 发布给 raw、RTMP、MP4 sink，并分别累计 sink 错误；但 `publish(false)` 当前没有让 pipeline 失败，实际写入仍在 encode_loop 同步调用，慢网络/磁盘会阻塞其它 sink。

### 8. MPP timeout 后能否释放输入？
- **合理回答：** 不能凭接口名假设。mpp_encoder.cpp 对 timeout/error 保守持有 pending buffer 到 teardown，但目标 SDK completion/flush 语义未验证。

### 9. Camera lease 和 MPP 输入是否跨同一生命周期？
- **合理回答：** 先 copy_nv12 到 vector，再复制到 MPP 自有 buffer，camera lease 在 copy 完后可释放；MPP 自有 buffer 的 completion 仍需板测。

### 10. pipeline 支持重复 run 吗？
- **合理回答：** run_started 使每个 DualCameraPipeline 实例单次使用，避免关闭队列后复用。

### 11. AI 慢会怎样？
- **合理回答：** AI queue 深度 2，满时丢旧并计数；compose 只使用不晚于当前帧的 snapshot。同步 RKNN 不可取消，stop 可能等待。

### 12. sink 写失败怎样处理？
- **合理回答：** FfmpegPipe::write 失败后会重开一次并重试；distributor 只做逐 sink 调用和计数，尚未真正实现异步隔离，`publish(false)` 也未传播为 pipeline failure。raw/RTMP/MP4 的最终 flush/close 检查仍需加强。

### 13. 是否零拷贝？
- **合理回答：** 不是完整零拷贝：V4L2 是 MMAP，EXPBUF 只导出 fd，后面有 packed CPU copy；DMA-BUF import、cache/fence 和异步完成未验证。

### 14. 是否真正调用 rkAIQ？
- **合理回答：** 没有。AiqController 的状态发现和 AE/AWB/曝光/增益接口在无 BSP backend 时返回 unsupported；无 rkAIQ headers/libs/IQ。

### 15. 哪些内容只是理论？
- **合理回答：** Sensor driver、DTS remote-endpoint、Media Controller link、RAW→YUV ISP、FSIN 同步、DMA-HEAP 零拷贝、双核调度、RTMP/MP4 播放和性能数字均是扩展或待验证。

## 第三轮：压力面

### 1. 为什么双路？
- **合理回答：** 目标是双视角合成并关联两路 AI 结果；双路带来同步、带宽、buffer 和编码压力。代码选择软件配帧和统一编码，硬件同步 fail-fast。

### 2. 为什么不用 USB Camera？
- **合理回答：** MIPI 可利用 RK3588 CSI/ISP，格式、延迟和同步可控；没有实测对比，不能说 MIPI 一定更快。

### 3. 两路是否硬件同步？
- **合理回答：** 不是当前已证实能力。FramePairer 只做软件时间戳匹配；hardware 模式无 backend 时明确报错。需要 FSIN/Trigger、DTS/driver、共享时钟和 SOF/示波器证据。

### 4. 为什么不使用两个 VPU？
- **合理回答：** 拼接后只有一个输出帧，单编码器减少重复工作；需要两路独立码流时才评估双编码器。当前没有性能对比。

### 5. 为什么两个 NPU context？
- **合理回答：** 两路各自维护输入、输出和结果时序，独立 context 隔离数据；core 参数没有接入，不能说已实现双核并行。

### 6. 为什么不训练两个不同模型？
- **合理回答：** 当前需求是同类检测，两个相同模型简化输入/后处理一致性；异构模型是产品选择，不是现有代码能力。

### 7. 为什么需要 RGA？
- **合理回答：** 颜色转换和缩放适合硬件加速，CPU fallback 便于验证；RGA 不等于零拷贝，实际输入来自 CPU packed buffer。

### 8. 为什么不直接 OpenCV？
- **合理回答：** OpenCV 便于验证和 fallback，RGA 可卸载常规颜色/resize；最终取舍要看目标板 benchmark。

### 9. 是否真正零拷贝？
- **合理回答：** 不是。V4L2 MMAP→copy_nv12→RGA/OpenCV/RKNN/MPP 工作缓冲；DMA-BUF import/cache/fence 未验证。

### 10. ISP 驱动是谁写的？
- **合理回答：** 仓库没有 Sensor/ISP 驱动，使用 BSP 提供的 V4L2 ISP 输出；我的工作是用户态接入、布局/时间戳校验、配帧和生命周期。

### 11. 改过哪些 DTS？
- **合理回答：** 当前没有 .dts/.dtsi，不能声称改过；拿到 BSP 后应提交 Sensor 节点、remote-endpoint、lane、时钟和供电 diff。

### 12. rkAIQ API 真的成功了吗？
- **合理回答：** 没有证据。AiqController setter 返回 unsupported，未加载真实 runtime/IQ，只能说预留控制边界。

### 13. 哪些能力没有上板验证？
- **合理回答：** C++/CTest、V4L2、MPP completion/flush、RGA import、RKNN 模型/核绑定、RTMP、MP4 播放、Sensor/ISP/rkAIQ、硬件同步、长稳和全部性能数字。

### 14. 和显微镜实习有什么区别？
- **合理回答：** 显微镜更偏单路低延迟传输和 RTSP/WebRTC；当前项目更偏双路 Camera、软件配帧、AI 时序、拼接和单编码包分发。两者都用 V4L2/MPP/RGA，但问题边界不同。

### 15. 去掉 AI 后还有什么价值？
- **合理回答：** 仍有双路 V4L2、配帧、Camera lease、RGA 拼接、单 MPP 编码和 RTMP/MP4 分发；no-ai 可用于分步验收和隔离问题。

## 压力面纪律

1. 先给结论，再给路径、函数和证据级别。
2. 主动区分源码实现、主机静态检查、C++ 构建、板端验证和性能测量。
3. 不把 wrapper 有 API 说成 pipeline 已接入；当前 NPU core 参数未生效。
4. 不把软件配帧、MMAP、RGA fallback、SDK 存在和日志输出说成硬件同步、零拷贝或性能通过。

