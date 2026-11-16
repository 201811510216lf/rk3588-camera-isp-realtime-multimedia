# 01 项目讲解稿

以下内容严格基于当前工作树。涉及硬件的部分必须主动说明“代码存在但尚未在 RK3588 板端验证”。

## 30 秒版本

我做的是一个面向 RK3588 的双路 Camera 用户态处理链路。两个 V4L2 video node 输出 NV12/NV12M，程序按驱动时间戳使用 `FramePairer` 做软件配帧，每路进入独立 AI 队列和 RKNN context，再经过 RGA/OpenCV 转换、左右拼接和 OSD，最后由一个 MPP H.264 encoder 编码，并把共享编码包送到 raw 文件、RTMP 和分段 MP4。我的重点是 V4L2 buffer lease、跨线程背压、双路帧时序和编码包分发。当前仓库没有 Sensor/DTS/rkAIQ/BSP 和板端日志，所以我不会把它说成已经完成 IMX415 硬件 bring-up。

## 1 分钟版本

入口在 `main.cpp:19-101`。程序注册信号处理，解析两个摄像头节点、输入输出尺寸、同步策略、模型和输出选项，组装 `DualCameraPipelineConfig` 后调用 `run()`。`run()` 在 `lib/dual_camera_pipeline.cpp:387-451` 中配置两个 `V4L2IspCamera`，读取已经由设备侧处理好的 NV12/NV12M 帧。

每个 capture loop 把 sequence、timestamp 和 clock domain 交给 `FramePairer`。匹配成功的 `FramePair` 进入 pair queue；AI 分支各自复制 NV12、转换成 BGR、调用 `rknn_lite::interf()`，并保存带原始 sequence/timestamp 的 snapshot。编码线程把两路画面缩放到左右 ROI，绘制框和状态文字，转回 NV12 后交给一个 MPP encoder。`EncodedPacketDistributor` 再把同一个应用层 payload 交给 raw、RTMP 和 MP4 sink。

我能用源码解释这条调用链，也有 Python 结构检查和分析器测试；但当前环境没有 CMake/C++ toolchain、RK3588、Sensor、模型、厂商 SDK、服务器或播放器。因此源码实现、主机测试和板端验证必须分开表述。

## 3 分钟版本

### 为什么采用 RK3588 和双路

目标是把两个视角组合成一个实时输出，同时保留两路 AI 结果。RK3588 具备 ISP、VPU、RGA 和 NPU 资源，但这些 SoC 能力不代表当前开发板已经布线、配置并验证。仓库从 V4L2 video node 开始，Sensor→CSI→RKCIF→RKISP 是设备侧前提。

### 程序如何启动

`main.cpp` 先解析 `AppOptions`。`lib/app_options.cpp:84-180` 支持 camera0/1、宽高、FPS、software/hardware sync、drop-older/hold-last、AI mode、model、record、RTMP 和 diagnostics。Linux 下把这些值复制到 `DualCameraPipelineConfig`，执行 `run()`，最终打印 frame/pair/encode/AI/sync 统计。

### 两路如何采集和配帧

`V4L2IspCamera::open()`（`lib/v4l2_isp_camera.cpp:203-482`）执行 capability、格式、REQBUFS、QUERYBUF、mmap 和可选 EXPBUF；`start()` QBUF 后 STREAMON；`dequeue()` 用 poll/DQBUF 形成 `CameraFrame`。Frame lease 复制到队列后，最后一个 lease 释放才触发 QBUF（`include/camera_frame.h:76-178`）。

`FramePairer`（`lib/frame_pairer.cpp:61-231`）只在 clock domain 已知且一致时比较 timestamp，在容差内选择最小 delta，并对乱序、超时、drop-older 和 hold-last 统计。它做的是软件配帧，不控制 Sensor 曝光。没有 FSIN/Trigger、SOF/曝光日志和硬件测量时，不能称为同步曝光。

### AI、拼接和编码

AI loop（`lib/dual_camera_pipeline.cpp:286-317`）先做 packed copy，再清空 frame lease，之后执行 RGA/OpenCV 和 RKNN。结果用 sequence/timestamp 和当前 pair 比较，避免把未来结果画到旧帧。`compose_pair`（`:319-364`）将两路 BGR 缩放到 `CompositeLayout` 的左右区域，画框、标签和状态文字，再转回 NV12。一个 MPP encoder 接收拼接后的输出；编码包由 distributor 送到多个 sink。

### 验证边界

当前可以证明用户态源码结构、静态检查和 Python 分析器；不能证明 Sensor driver、DTS、RKISP media graph、rkAIQ context/IQ、AE/AWB、真实模型、MPP completion、RTMP/MP4 播放或硬件同步曝光。`--sync-mode hardware` 在没有 verified backend 时会 fail-fast。

## 5～8 分钟深入版本

### 1. 从 main 到 camera

`main()` 注册 SIGINT/SIGTERM，并在 Linux 下忽略 SIGPIPE。参数解析成功后创建 `DualCameraPipeline`。`run()` 先阻止重复运行和未验证的 hardware sync，然后分别 `set_config/open/start` 两个 camera，比较实际协商到的尺寸和 FPS，建立输出布局并初始化输出 sink、MPP header 和可选模型。

### 2. CameraFrame 与生命周期

V4L2 slot 状态是 Queued→Dequeued→InUse→Queued。`CameraFrame` 包含描述信息、owner 和 lease；队列复制的是 shared ownership，消费者不关闭 fd/mmap。AI 分支在复制 packed NV12 后立即释放 frame，编码分支在完成 compose/encode 后释放 pair。MPP 自有 buffer 的完成语义与 V4L2 lease 是两个生命周期，不能因为 `encode_put_frame()` 返回就假设硬件已经停止读取。

### 3. 配帧和背压

pair queue 深度为 4，AI queue 每路深度为 2；满时丢旧并统计。`drop-older` 优先选择时间接近的帧，`hold-last` 只提供 stale 降级。slow AI 会丢 AI 帧，但同步 RKNN 调用没有 SDK 取消；slow sink 在当前实现中仍可能阻塞 encode thread。

### 4. RKNN 与 OSD

`rknnPool.hpp` 有真实 API 调用形状，包括 init、query、run、outputs_get/release，但当前没有 runtime、模型和 target build 证据。双路入口在 `dual_camera_pipeline.cpp:435` 对两个 context 都传 `n=0`，所以不能声称 `--npu-core0/1` 已生效。AI snapshot 的 sequence/timestamp 关联是源码设计，实际模型输出和后处理仍需验证。

### 5. RGA、MPP 和多目的分发

RGA 优先做 NV12↔BGR/resize，OpenCV 作为 fallback；拼接完成后转回 NV12。MPP 轮询 packet 并保守处理 timeout/error；SPS/PPS 通过 `GetHeader()` 写入输出。`FfmpegPipe` 用 stdin 管道给 ffmpeg，distributor 共享 `std::shared_ptr<const std::vector<uint8_t>>`，但 sink 调用仍在同一编码线程同步发生，所以“共享 payload”不等于“异步互不阻塞”。

### 6. 收尾表达

这个项目最适合被描述为“有真实源码的双路 Linux Camera 用户态处理链路和 IMX415 ISP 适配准备”。它不应被描述为“已经完成 IMX415 Sensor/ISP/rkAIQ/AE/AWB 或性能验证”。

## 常见追问的诚实回答

- **ISP 是你写的吗？** 设备侧 ISP 属于 BSP/硬件；仓库接收 ISP 输出的 NV12/NV12M，并提供探测和离线分析工具。
- **两路是否硬件同步？** 不是当前证据；实现的是软件时间戳配帧，hardware mode 没有 backend 时 fail-fast。
- **是否零拷贝？** 不能这样说。当前路径做 CPU packed copy，MPP 也使用自有输入 buffer。
- **rkAIQ 是否调用成功？** 当前没有真实 rkAIQ backend，控制接口返回 Unsupported。
- **有多少 FPS/延迟提升？** 没有可复核板端测量，不能编造数字。
