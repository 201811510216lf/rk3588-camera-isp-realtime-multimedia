# Codex 自动执行任务书：RK3588 双路 Camera ISP 大鼠行为监测系统

> 将本文件放在当前仓库根目录，命名为 `CODEX_AUTOPILOT_PLAN.md`。然后让
> Codex 完整阅读并执行。\
> **执行要求：阶段 1 至阶段 10 连续自动执行，不等待用户确认。**
> 每阶段读取前序报告，生成本阶段报告，再进入下一阶段。硬件缺失时标记
> `BLOCKED_HARDWARE`，继续软件工作，不得伪造硬件结果。

## 0. 总任务与项目边界

你是资深嵌入式 Linux、C++17、Rockchip Camera、V4L2、RGA、MPP、RKNN
工程师。

### 项目定位

**基于 RK3588 的双路 Camera 实时图像处理与大鼠 24
小时智能行为监测系统。**

业务背景：实验室需要连续 24
小时监控大鼠，通过双路摄像头采集活动图像，识别实验定义的行为，并保存视频和结构化事件。

### 唯一主链路

``` text
Camera Sensor 0                         Camera Sensor 1
       |                                       |
       +-------------------+-------------------+
                           |
                    MIPI / RKCIF
                           |
                     RK3588 RKISP
                 (硬件 ISP + RkAiq 控制)
                           |
                V4L2 ISP 输出 NV12/NV12M
                           |
              +------------+------------+
              |                         |
        Camera Capture 0           Camera Capture 1
              |                         |
        Frame/Buffer Lease         Frame/Buffer Lease
              |                         |
        Bounded Queue 0            Bounded Queue 1
              |                         |
              +------------+------------+
                           |
                Frame Dispatch / Pairing
                           |
              +------------+-------------+
              |                          |
         AI Inference Path          Video Path
              |                          |
        RKNN Runtime                RGA Processing
              |                    Resize/Convert/Compose
        Rat Detection                     |
        Behavior Analysis              OSD Overlay
              |                          |
        Behavior Result                  |
              +------------+-------------+
                           |
                 Result-to-Frame Association
                           |
                    MPP H.264 Encoder
                           |
             +-------------+--------------+
             |             |              |
          MP4 Recorder   RTSP/RTMP     Event Index
             |             |              |
             +-------------+--------------+
                           |
                  24h Monitoring Manager
```

约束： - RkAiq 属于 ISP 的 3A/IQ
控制软件，不是图像数据流中的串行硬件节点。 - 主业务输入固定为两路真实
Camera，经 RK3588 硬件 ISP 后由 V4L2 输出 NV12/NV12M。 - 不增加
USB、文件、RTSP、Mock Camera
等其他运行时输入源，不把它们设计为产品运行模式。 -
单元测试可构造内存测试帧，但测试 Fixture 不是产品输入源。 -
当前无开发板时，硬件后端只能实现接口、构建隔离、配置和诊断能力；不得声称真实
V4L2、RKISP、RkAiq、RGA、MPP、RKNN 已经板端验证。 -
软件架构、业务逻辑、测试和文档必须尽可能完整实现，不能因硬件不可用停止任务。

### Software ISP 决策

原 `camera_isp_demo` / VirtualSensor / RAW10 Software ISP： -
保留现有代码和独立实验能力； - 不接入主业务 Pipeline； - 不把 RKISP 输出
NV12 再送入 BLC、DPC、LSC、Demosaic 等 RAW 域算法； - 本轮不扩展
Software ISP 算法，不删除已有成果； - 在文档中说明它是独立实验工具。

## 1. 参考工程：RK-MediaProject

参考仓库：<https://github.com/JoeChen2me/RK-MediaProject>

已核对的工程组织： - 根目录包括
`src/`、`include/`、`lib/`、`Plan/`，以及
`CMakeLists.txt`、`AGENTS.md`、`README.md`、`.clang-format`。 - `src/`
放实现，如
`main.cpp`、`v4l2Camera.cpp`、`rkmppdec.cpp`、`rkrga.cpp`、`rkmppenc.cpp`、`zlm_publisher.cpp`。 -
`include/` 放公共头文件；`lib/` 放必要的本地库；`Plan/`
放设计和接入计划。 - 使用
CMake、C++17、`Threads::Threads`、`-Wall -Wextra -Wpedantic`。 -
流水线采用 Capture、Decode、RGA、Encode Input、Encode Output
等线程解耦，并使用队列/recycle queue 管理 Buffer。 - 其链路是 USB MJPEG
→ MPP Decode → RGA → MPP H.264 → ZLMediaKit。**本项目输入是 RKISP
已处理的 NV12/NV12M，因此不需要照搬 MPP MJPEG Decode 阶段。** -
不固定照搬 `/dev/video0`、1080p30、MJPEG、硬编码参数或其测试数据。 -
不复制参考仓库源码或第三方二进制；仅借鉴目录组织、模块边界、线程流水线、资源回收和
C++ 风格。引用代码时遵守其许可证。

参考： - <https://github.com/JoeChen2me/RK-MediaProject> -
<https://github.com/JoeChen2me/RK-MediaProject/blob/master/CMakeLists.txt> -
<https://github.com/JoeChen2me/RK-MediaProject/blob/master/AGENTS.md>

## 2. 自动执行协议

1.  先阅读本文件、仓库 `AGENTS.md`、README、`Plan/` 和现有架构文档。
2.  阶段 1 至阶段 10 必须按顺序连续执行，不等待用户确认。
3.  每阶段开始时读取前序阶段报告和代码。
4.  每阶段结束时生成报告、执行构建/测试，然后自动进入下一阶段。
5.  遇到普通软件错误，尝试修复并重测。
6.  遇到 SDK/硬件缺失，标记
    `BLOCKED_HARDWARE`，实现可独立完成的软件部分后继续。
7.  不得伪造构建、测试、性能、模型精度或板端结果。
8.  不得删除用户数据、已有模型或已有实验代码。
9.  不得把空函数、Mock 或接口声明报告为真实硬件实现。
10. 最终输出修改文件清单、真实测试结果、未完成项和板端适配清单。

------------------------------------------------------------------------

# 阶段 1：仓库审计与参考工程结构分析

## 工作

-   遍历所有源码、头文件、CMake、脚本、测试、README、架构文档。
-   阅读 `AGENTS.md`、`CURRENT_ARCHITECTURE.md`、`myDemo` 和
    `camera_isp_demo`。
-   检查线程、队列、V4L2 DQBUF/QBUF 生命周期、MPP/RGA Buffer
    生命周期、Sink 阻塞风险。
-   标记模块状态：
    -   `IMPLEMENTED`
    -   `PARTIAL`
    -   `INTERFACE_ONLY`
    -   `MOCK_OR_DEMO`
    -   `HARDWARE_UNVERIFIED`
    -   `MISSING`
-   对照 RK-MediaProject 的
    `src/include/lib/Plan`、CMake、C++17、线程流水线和 recycle queue
    组织。
-   识别哪些代码应复用，禁止无意义重写。

## 产物

-   `Plan/00_REPOSITORY_AUDIT.md`
-   `Plan/00_CURRENT_MODULE_STATUS.md`
-   `Plan/00_REFERENCE_PROJECT_ANALYSIS.md`
-   `Plan/00_REFACTOR_ROADMAP.md`

完成后自动进入阶段 2。

------------------------------------------------------------------------

# 阶段 2：统一工程结构与构建系统

## 目标

建立可维护、硬件依赖隔离、后续能适配瑞芯微官方 RK3588 BSP 的 C++17
工程。

目录建议（结合现有目录做最小迁移，不要机械创建重复目录）：

``` text
project/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── Plan/
├── include/
│   ├── common/
│   ├── camera/
│   ├── media/
│   ├── pipeline/
│   ├── ai/
│   ├── image/
│   ├── encoder/
│   ├── output/
│   ├── behavior/
│   └── runtime/
├── src/
│   ├── main.cpp
│   ├── common/
│   ├── camera/
│   ├── media/
│   ├── pipeline/
│   ├── ai/
│   ├── image/
│   ├── encoder/
│   ├── output/
│   ├── behavior/
│   └── runtime/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── stress/
├── tools/
├── configs/
├── models/
├── scripts/
├── docs/
├── lib/
└── camera_isp_demo/
```

## 要求

-   C++17，保留当前兼容的 CMake 最低版本。
-   生成 `compile_commands.json`。
-   使用 `Threads::Threads`。
-   `.h/.cpp` 分离。
-   公共接口放 `include/`，实现放 `src/`。
-   `Plan/` 保存阶段计划和审计产物。
-   沿用 `.clang-format`，没有则新增。
-   编译警告 `-Wall -Wextra -Wpedantic`。
-   Rockchip SDK 采用可选 CMake 开关：
    -   `ENABLE_V4L2`
    -   `ENABLE_ROCKCHIP_RGA`
    -   `ENABLE_ROCKCHIP_MPP`
    -   `ENABLE_RKNN`
    -   `ENABLE_RKAIQ`（仅确有应用侧 SDK 集成需要时）
-   当前无 SDK 时，核心软件和单元测试仍可构建。
-   SDK 开关打开但依赖缺失时输出清楚错误。
-   不允许用空函数返回 success 冒充硬件功能。
-   保留现有主程序兼容入口，迁移后再清理重复代码。

生成 `Plan/01_BUILD_AND_LAYOUT.md`，构建并测试后自动进入阶段 3。

------------------------------------------------------------------------

# 阶段 3：统一图像数据与双路 V4L2 Camera 软件层

## 目标

主输入固定为两路 RKISP 输出的 NV12/NV12M。不要增加其他运行时输入源。

## 3.1 统一帧结构

检查并复用现有
`CameraFrame`、`ImageFrame`、`FrameSample`，避免重复定义。

统一表达： - `camera_id` - `sequence` - `source_timestamp_ns` -
`monotonic_timestamp_ns` - `width` - `height` - `pixel_format` -
`plane_count` - 每个 plane 的 `stride` - `offset` - `length` -
`bytes_used` - `buffer_index` - Buffer lease/ownership -
`dma_fd`（仅为未来适配预留，不得伪称已实现 DMA-BUF）

必须支持 NV12 单平面和 NV12M 多平面，正确处理实际
stride、alignment、plane size 和 bytesused。禁止默认
`size = width * height * 3 / 2` 就是真实 Buffer 大小。

## 3.2 V4L2 Capture

复用或完善 V4L2 Camera： - open； - QUERYCAP； - ENUM_FMT； - S_FMT； -
REQBUFS； - QUERYBUF； - MMAP； - QBUF； - STREAMON； - DQBUF； -
STREAMOFF； - munmap； - close。

配置读取设备节点、分辨率、帧率、格式和 Buffer 数量。禁止硬编码
`/dev/video0`、`/dev/video1`。

应用读取 RKISP 已处理输出节点；应用层不实现 ISP，不把 NV12 当 RAW10。

## 3.3 双路管理

实现： - CameraManager； - 两路独立 Capture Thread； -
两路独立有界队列； - 独立 FPS/drop/error 统计； - 独立启停和故障状态； -
一路故障时尽可能隔离另一路。

## 3.4 Buffer 所有权

DQBUF 后形成明确的 Buffer lease。消费者使用完成前不得
QBUF。最后一个消费者释放 lease 后才能回收。shutdown
顺序必须是停止生产、关闭队列、等待消费者退出、最后释放映射和设备。

禁止重复 QBUF、重复释放、悬空指针和提前回收。

## 产物

-   `Plan/02_V4L2_DUAL_CAMERA.md`
-   `docs/V4L2_BUFFER_LIFECYCLE.md`

无硬件时完成接口、编译隔离和可测的数据层，板端状态标为
`BLOCKED_HARDWARE`。然后自动进入阶段 4。

------------------------------------------------------------------------

# 阶段 4：有界队列、双路帧调度和 FramePairer

## 目标

复用现有 FramePairer，完善双路实时调度。

## BoundedQueue

支持： - push / tryPush； - pop / tryPop / waitPop； - close； -
timeout； - size / capacity； - high-water mark； - push/pop/drop 计数。

策略： - `BLOCK` - `DROP_OLDEST` - `DROP_NEWEST` - `LATEST_ONLY`

队列必须有界。不同业务按需求配置策略。

## FramePairer

支持： - camera_id； - sequence； - source timestamp； - monotonic
timestamp； - 时钟域校验； - tolerance； - timeout； - drop-older； -
hold-last； - 最大队列深度； - 配对成功率； - 最大时间差； - P50/P95/P99
时间差。

明确软件时间戳配帧不等于硬件曝光同步。

## 业务模式

-   双视角联合分析；
-   两路独立推理和录像。

独立录像不能等待另一 Camera 配对。

## 测试

使用内存测试帧测试正常配对、漂移、掉帧、延迟、乱序、队列满、超时和关闭唤醒。

产物：`Plan/03_FRAME_SCHEDULING.md`。然后自动进入阶段 5。

------------------------------------------------------------------------

# 阶段 5：RKNN AI Pipeline 与大鼠行为分析

## 目标

复用现有 AI Backend
和异步队列，实现清晰的检测、跟踪、行为分析、事件输出分层。

## 数据流

``` text
ISP NV12/NV12M
  -> AI Queue
  -> Preprocess
  -> IAiBackend
  -> Postprocess
  -> DetectionResult
  -> Tracking
  -> Behavior Analyzer
  -> Behavior Event
```

## Backend

检查现有 `IAiBackend`、`DemoBackend` 和 RKNN 代码： - 复用已有接口； -
Demo 仅用于接口测试，不得作为真实大鼠识别结果； - 实现 RKNN Backend
适配边界； - SDK 可选构建； - 根据实际 RKNN Runtime 头文件实现，不猜测
API； - SDK 不存在时标记 `RKNN_BACKEND_UNAVAILABLE`。

## 预处理

正确处理 NV12/NV12M、stride-aware
crop、resize/letterbox、RGB/BGR、normalize、tensor layout
和量化参数。输入尺寸、通道顺序、归一化必须来自模型配置/metadata，不得猜测。

## 结果结构

至少包含： - camera_id； - sequence； - timestamp； - bbox； -
class_id； - confidence； - model_id/version。

## 行为分析

分层实现： 1. Rat Detection； 2. Tracking； 3. Behavior Classification；
4. Temporal Analysis； 5. Event Manager。

不要声称单帧 YOLO
能识别所有连续行为。行为类别由配置定义。没有真实模型时不得伪造准确率。

## 异步调度

-   AI Queue 有界；
-   可丢弃过期推理帧；
-   结果绑定原始 frame metadata；
-   AI 不得阻塞录像；
-   统计 inference latency/FPS/queue depth/drop count；
-   Context 数量由真实 RKNN/NPU 资源决定，不盲目创建。

产物： - `Plan/04_AI_PIPELINE.md` - `docs/BEHAVIOR_RECOGNITION.md` -
`docs/RKNN_BACKEND_ADAPTATION.md`

自动进入阶段 6。

------------------------------------------------------------------------

# 阶段 6：RGA、OSD、合成和 MPP 编码

## 目标

复用当前 RGA/MPP，参考 RK-MediaProject 的线程流水线、输入输出队列和
recycle queue。

本项目 ISP 输出 NV12/NV12M，**不需要 MJPEG MPP Decode 阶段**。

## Video Pipeline

``` text
ISP NV12/NV12M
      |
  Frame Dispatch
      |
      +-------------------+
      |                   |
  AI Inference        Video Queue
      |                   |
  Behavior Result     RGA Processing
      |               Resize/Convert
      |                   |
      +--------+----------+
               |
       Result-to-Frame Match
               |
              OSD
               |
      Optional Dual View Compose
               |
        MPP Encode Input
               |
        MPP Encode Output
               |
          H.264 Packet
```

## RGA

-   复用现有代码；
-   明确输入/输出格式；
-   正确处理 NV12/NV12M；
-   正确处理 stride/height alignment；
-   支持 SDK 实际提供的 resize/crop/convert/compose；
-   管理输入输出 Buffer 生命周期；
-   SDK 缺失时保留硬件 Backend 边界；
-   不得用 CPU 实现冒充 RGA。

## OSD

绘制： - rat bounding boxes； - class label； - confidence； - camera
id； - timestamp； - behavior state。

结果必须与对应帧匹配。过期结果按配置丢弃或标记 stale。

## MPP

复用现有 MPP Encoder，检查： - MppFrame； - NV12/NV12M； -
hor_stride/ver_stride； - 输入 Buffer； - 输出 MppPacket； - PTS； -
GOP； - bitrate； - CBR/VBR； - flush/EOS； - 错误恢复； - 资源释放。

MPP SDK 不存在时，核心软件测试仍能构建；硬件编码必须标记未验证，不能用
Mock 冒充 MPP。

## 线程

参考 Capture、RGA、Encode Input、Encode Output 分离方式；增加必要的
recycle queue。不得添加无用 Decode Worker。

产物：`Plan/05_RGA_MPP_VIDEO_PIPELINE.md`。自动进入阶段 7。

------------------------------------------------------------------------

# 阶段 7：MP4/RTSP/RTMP 输出与独立 Sink

## 目标

复用现有输出能力，完善 24 小时录像。

## 结构

``` text
MPP H.264 Packet
       |
Encoded Packet Dispatcher
       |
       +----------------+
       |                |
 MP4 Recorder      Network Publisher
       |                |
 Segment Files      RTSP/RTMP
```

## MP4

-   分段时长可配置；
-   文件命名包含时间；
-   正确处理关键帧/GOP；
-   PTS/DTS/time_base 正确；
-   正常 finalize；
-   异常退出文件检查；
-   磁盘空间监控；
-   录像索引；
-   同一 muxer 不允许多线程无保护写入。

## Network Publisher

-   独立线程；
-   连接超时；
-   断线重连；
-   有界队列；
-   网络阻塞不得影响 Camera Capture 和 MPP 编码；
-   统计发送失败、丢包/丢弃和重连。

优先复用现有 FFmpeg/发布库，不无故替换协议栈。

## 时间戳

区分 source timestamp、monotonic timestamp、encoder PTS、muxer
time_base、wall-clock。不得将纳秒时间戳直接当 MP4 PTS。

产物：`Plan/06_RECORDING_AND_STREAMING.md`。自动进入阶段 8。

------------------------------------------------------------------------

# 阶段 8：大鼠行为事件与 24 小时运行管理

## Behavior Event

事件字段： - event_id； - camera_id； -
rat_id（仅模型支持个体识别时使用）； - behavior_type； -
start_timestamp； - end_timestamp； - duration_ms； - confidence； -
source frame sequence； - video segment id； - video time offset； -
model version。

状态机考虑： - 连续帧平滑； - 最小持续时间； - 置信度阈值； -
事件开始/结束； - 短暂漏检； - 目标丢失； - 重复事件合并。

无真实行为模型时不得输出虚构行为结果。

## 录像关联

每个行为事件能够定位到 Camera、录像分段、视频起止时间和帧序号。

AI 可以按策略丢弃过期推理帧，但不得因 AI 慢而丢弃必须保存的实验录像帧。

## Long Running Manager

实现： - Pipeline Health Monitor； - Camera heartbeat； - AI
heartbeat； - Encoder heartbeat； - Sink heartbeat； - queue depth； -
CPU/RSS； - 磁盘空间； - 可用时采集温度/频率； - 异常计数； -
日志轮转； - shutdown 管理。

故障隔离： - Network Sink 故障不停止采集； - AI 故障不停止录像； - 单路
Camera 故障独立报告； - 恢复有次数限制和退避； - 禁止无限快速重启。

存储： - 分段录像； - 磁盘阈值告警； - 保留策略配置； - 行为事件
JSONL/CSV； - 录像索引； - 默认禁止自动删除尚需保存的实验原始数据。

产物： - `Plan/07_BEHAVIOR_EVENT_MANAGER.md` -
`Plan/07_LONG_RUNNING_RUNTIME.md`

自动进入阶段 9。

------------------------------------------------------------------------

# 阶段 9：构建、测试、静态分析和压力测试

## 构建

执行仓库实际支持的 CMake 构建。

-   C++17；
-   `-Wall -Wextra -Wpedantic`；
-   `compile_commands.json`；
-   Rockchip SDK 不存在时关闭硬件选项；
-   SDK 缺失不能阻止核心软件和单元测试构建；
-   不得将未构建硬件模块报告为成功。

## 单元测试

测试： - ImageFrame； - NV12/NV12M plane layout； - stride； - Buffer
lifetime； - BoundedQueue； - FramePairer； - timestamp； -
DetectionResult； - Behavior State Machine； - Event Manager； - Packet
timestamp conversion； - Segment naming/index。

## 集成测试

用内存测试帧作为测试 Fixture，验证： - 双路帧进入队列； -
两路独立调度； - 软件配帧； - AI 结果关联； - OSD 结果关联； -
编码输入队列； - Packet 分发； - Event 与录像索引关联。

不得新增其他运行时 Camera Source。

缺少真实 RKNN/MPP/RGA SDK 时，不得伪造硬件测试成功。

## Sanitizer

环境支持时执行 ASan、UBSan、TSan、静态分析、clang-format。

## 压力测试

测试： - 多线程队列压力； - 生产者快于消费者； - 消费者快于生产者； -
队列满； - 线程提前退出； - 反复 start/stop； - Buffer 异常释放； -
Event Manager 长时间事件序列。

生成： - `Plan/08_TEST_STRATEGY.md` - `Plan/08_TEST_RESULTS.md` -
`scripts/run_tests.sh` - `scripts/run_stress_tests.sh`

准备 24h/72h 板端测试计划。PC 测试不得标记为 RK3588 板端测试。

自动进入阶段 10。

------------------------------------------------------------------------

# 阶段 10：文档、架构图和硬件适配交付

生成或更新：

1.  `README.md`：业务背景、架构、构建、配置、状态和硬件边界。
2.  `AGENTS.md`：目录、代码规范、构建、测试、硬件适配约束。
3.  `Plan/FINAL_ARCHITECTURE.md`：Mermaid
    架构图、线程模型、数据流、Buffer 所有权。
4.  `Plan/HARDWARE_ADAPTATION_ROADMAP.md`：瑞芯微官方 RK3588
    BSP、V4L2、Media Controller、RKISP30、RkAiq、RGA、MPP、RKNN、DMA-BUF
    后续适配清单。
5.  `Plan/PROJECT_STATUS.md`：已实现、部分实现、仅接口、SDK
    缺失、硬件待验证、剩余风险。
6.  `Plan/PERFORMANCE_METRICS.md`：FPS、latency、P50/P95/P99、queue
    depth、drop count、CPU/RSS、编码吞吐和事件吞吐。
7.  `Plan/INTERVIEW_GUIDE.md`：架构、V4L2、ISP/RkAiq、Buffer、多线程、RKNN、RGA、MPP、24h
    长稳。
8.  `Plan/FINAL_DELIVERY_REPORT.md`：全部修改和真实测试结果。

README 必须准确区分： - 软件实现完成； - 单元测试通过； - SDK
编译状态； - RK3588 硬件验证状态； - 双路真实 Camera 状态； - RKNN
模型部署状态； - 24 小时板端稳定性状态。

------------------------------------------------------------------------

# 全局工程规范

## 参考 RK-MediaProject 的组织方式

-   `src/`：C++ 实现；
-   `include/`：公共头文件；
-   `lib/`：合法且必要的本地依赖；
-   `Plan/`：设计与接入计划；
-   CMake；
-   C++17；
-   `.h/.cpp` 分离；
-   Capture/RGA/Encode Input/Encode Output 线程解耦；
-   ready/recycle queue；
-   明确资源回收；
-   优雅退出。

不照搬 USB MJPEG → MPP Decode。主链路是 RKISP 输出 NV12/NV12M。

## 硬件隔离

使用清晰边界： - V4L2 Camera Source； - RGA Image Processor； - RKNN AI
Backend； - MPP Video Encoder； - RkAiq
Controller（仅在确有应用侧集成需求时）。

SDK 依赖可选。空实现不得返回成功。

## 内存与实时性

-   明确 Buffer 所有权；
-   避免无谓图像复制；
-   不声称零拷贝，除非实现并验证；
-   正确处理 NV12/NV12M stride/alignment；
-   队列有界；
-   AI 不阻塞录像；
-   网络不阻塞编码；
-   关键帧和录像时间戳正确；
-   线程退出时资源完整回收。

## 业务真实性

-   目标检测不等于行为识别；
-   单帧分类不等于连续行为分析；
-   行为事件必须有起止时间；
-   事件必须能关联录像；
-   模型不存在时不得伪造结果；
-   测试结果不得伪造。

## 最终验收目标

软件阶段应具备： 1. 双路 Camera 输入接口； 2. RK3588 ISP 输出格式约定；
3. 统一 Frame/Buffer 管理； 4. 双路采集调度； 5. FramePairer； 6. AI
推理 Pipeline； 7. 大鼠行为事件管理； 8. RGA 图像处理接口； 9.
OSD/双路合成； 10. MPP 编码接口； 11. MP4/RTSP/RTMP 输出（按现有依赖）；
12. 24 小时运行管理； 13. 自动化测试； 14. 完整架构和官方 BSP 适配文档。

**现在立即从阶段 1 开始，连续执行阶段 1 至阶段
10，不要在阶段之间等待用户确认。完成后输出最终交付报告，明确哪些部分还需要在瑞芯微官方
RK3588 BSP 和真实双路 Camera 上验证。**
