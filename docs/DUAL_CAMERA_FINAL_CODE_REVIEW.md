# RK3588 双路 Camera/ISP + 实时 AI 拼接系统第四轮严格工程审查

> **历史审查：2026-11-13 第四轮双路源码快照。**
>
> 本报告保留审查时的 Git 状态、源码行号、缺陷和构建限制。后续软件 ISP、AE/AWB 与其他修正不能从本报告推断为不存在或已验收；其中“当前”和“NOT_IMPLEMENTED”等结论仅限当时范围，具体问题应对照最新源码和测试重核。
>
> 现行依据：[当前架构](CURRENT_ARCHITECTURE.md) · [执行与验证状态](CODEX_EXECUTION_STATUS.md)。范围说明更新：2026-11-16（Asia/Shanghai）。

审查日期：2026-11-13（Asia/Shanghai）  
审查对象：`demo_multhread_decode_infer_mulmodel` 当前工作区、`HEAD=6085dab` 及其之后的未提交改动。  
审查范围：源码、CMake、脚本、已有文档、Git 状态、可执行的主机检查和独立 FFmpeg 复核。  
审查限制：本轮只读；没有修改业务代码。当前主机是 Windows，没有 CMake/C++ 编译器、Linux V4L2 设备、RK3588 BSP、RKNN/MPP/RGA/OpenCV 厂商 SDK、Sensor 资料、IQ 文件、模型文件或 RTMP 服务器。

## 1. 总体结论

当前版本已经从旧的单路/Legacy 结构收敛出一条双路用户态源码链路：入口在 `main.cpp:42-97`，分别创建两个 `V4L2IspCamera`（`lib/dual_camera_pipeline.cpp:191-192,395-406`），各自使用采集线程（`238-284,445`），将 V4L2 帧送入 `FramePairer`（`255-261`），再进入双 AI 队列、CPU/RGA 转换、左右合成、一个 MPP Encoder 和共享 packet distributor（`286-379`）。这证明了主要接口和调用关系已写入源码。

这些代码还不能被称为完成的 RK3588 产品链路。结论如下：

| 问题 | 结论 |
| --- | --- |
| 双路 Camera 是否真实实现 | **源码级 PARTIAL**。有两个实例、两个节点参数和两个线程；未验证设备，且没有拒绝两个参数指向同一设备或同一物理实体。 |
| FramePairer 是否正确 | **PARTIAL**。有界队列、容差、超时、乱序和百分位计算存在；不同时钟域无候选时仍用原始数值决定丢哪一路，违反跨域禁止直接比较的约束。 |
| 双 RKNN 是否真实运行 | **源码级 PARTIAL / NOT_VERIFIED**。有两个独立对象和线程，但构造时两个 Context 都传入 `n=0`，均绑定 Core 0；模型输入布局、类别数和输出 shape 未按真实模型验证。 |
| OSD 和拼接是否正确 | **PARTIAL**。左右偏移和几何映射有实现；RGA 只用于颜色转换/缩放尝试，OSD 和文字实际由 OpenCV CPU 完成；无板端画面证据。 |
| RTMP 与 MP4 是否共享编码结果 | **源码上是同一 `shared_ptr` packet**，但 sink 是同步顺序调用，没有每路有界队列；网络或磁盘阻塞会回压编码，且没有 RTMP 服务端、播放器或 ffprobe 的项目输出验证。 |
| ISP 软件开发深度 | **NOT_IMPLEMENTED / BLOCKED_BY_MISSING_BOARD_INFO**。没有 Sensor driver、DTS/DTSI、rkAIQ API 调用或 IQ 匹配；只有只读探测器和 fail-closed 控制壳。 |
| C++ 构建 | **NOT_EXECUTED**。`cmake`、`ctest`、g++/clang++/MSVC 均不可用，且 vendor 目录不存在。 |
| 真实板端运行证据 | **没有**。`BOARD_TESTED`、`TARGET_BUILT`、`MEASURED` 均不能成立。 |

独立四项结论：

- `SOURCE_IMPLEMENTATION=PARTIAL`：双路用户态骨架、配帧、合成、编码和 packet 共享代码存在，但若干关键契约仍不正确。
- `BUILD_TEST_STATUS=HOST_STATIC_PASS_CXX_NOT_EXECUTED`：Python 结构/语法检查通过；C++/CMake/CTest 未执行。
- `HARDWARE_SYNC_STATUS=NOT_VERIFIED`：`--sync-mode hardware` 在 `lib/dual_camera_pipeline.cpp:392` 明确 fail-fast；软件配帧不等于同步曝光。
- `END_TO_END_BOARD_STATUS=AWAITING_BSP_BOARD_SERVER`：无 BSP、Sensor、IQ、板端帧、RTMP 服务端或播放器证据。

## 2. Git 基线和本轮真实改动

仓库根目录是 `demo_multhread_decode_infer_mulmodel`，外层目录不是 Git 根。实际命令结果：

```text
git status --short       -> 12 个 tracked 文件修改，报告创建前约 46 个未跟踪新增文件
git log -5 --oneline     -> 6085dab, 6c51a51, 7b51ff8, 9bd2aaa（共 4 个提交）
git diff --stat           -> 12 个 tracked 文件，351 insertions / 224 deletions
git diff --check          -> exit 0（仅有 LF->CRLF 警告，无 whitespace error）
```

关键未跟踪新增文件包括：

- `include/dual_camera_pipeline.h`、`include/frame_pairer.h`、`include/composite_processor.h`、`include/encoded_packet_fanout.h`、`include/aiq_controller.h`、`include/media_graph_validator.h`；
- `lib/dual_camera_pipeline.cpp`、`lib/frame_pairer.cpp`、`lib/composite_processor.cpp`、`lib/encoded_packet_fanout.cpp`、`lib/aiq_controller.cpp`、`lib/media_graph_validator.cpp`；
- `tests/frame_pairer_test.cpp`、`tests/composite_processor_test.cpp`、`tests/encoded_packet_fanout_test.cpp`、`tests/media_graph_validator_test.cpp`；
- `docs/CODEX_EXECUTION_STATUS.md`、`docs/SYNC_DESIGN.md`、`docs/BSP_CAMERA_ISP.md`、`docs/BOARD_ACCEPTANCE.md`、`docs/PIPELINE_AND_OWNERSHIP.md`、`docs/PERFORMANCE.md` 等。

创建本报告后，未跟踪列表还包含本报告自身；当前 `git ls-files --others --exclude-standard` 为 47 个。不能因为 `git diff` 只显示 tracked 文件就忽略新增主链路；本报告同时审查了未跟踪源码。

## 3. 功能真实性矩阵

| 功能 | 状态 | 证据 | 真实性判断 |
| --- | --- | --- | --- |
| 双路独立 V4L2 实例 | **PARTIAL** | `lib/dual_camera_pipeline.cpp:191-192,395-406` | 两个对象和不同配置存在；未拒绝相同节点/同一物理设备，未在 Linux 运行。 |
| 双独立采集线程 | **PARTIAL** | `lib/dual_camera_pipeline.cpp:238-284,445` | 每路线程独立 DQBUF；线程只在主机源码中存在，未实测断流隔离。 |
| NV12/NV12M plane/stride/bytesused | **PARTIAL** | `lib/v4l2_isp_camera.cpp:240-380,587-696`，`dual_camera_pipeline.cpp:48-75` | 有实际校验和 offset 保存；未做真实驱动/非零 offset/NV12M 边界测试。 |
| V4L2 时间戳域 | **PARTIAL** | `lib/v4l2_isp_camera.cpp:640-655` | 保留 flags；非 MONOTONIC 的所有情况被归为 REALTIME，未知/不可信来源没有单独保留。 |
| FramePairer | **PARTIAL** | `lib/frame_pairer.cpp:88-230` | 有界、容差、超时、乱序、统计存在；跨域拒绝路径仍比较原始 timestamp。 |
| 双 RKNN Context | **PARTIAL / NOT_VERIFIED** | `dual_camera_pipeline.cpp:196,435,444` | 有两个对象/线程；构造传 `n=0`，两个 Context 都设 Core 0，CLI core 参数无效。 |
| RKNN 输入/后处理 | **PARTIAL** | `include/rknnPool.hpp:125-209,223-307`，`lib/postprocess.cpp:114-147,283-395` | 只接受 1 input、至少 3 output，强制 NHWC，pipeline 固定 `class_num=1`；模型 shape/类别未实测，NMS 类别索引存在逻辑错误。 |
| OSD/左右拼接 | **PARTIAL** | `dual_camera_pipeline.cpp:319-364`，`lib/composite_processor.cpp:70-151` | 坐标缩放和 Camera1 左右偏移有实现；OSD/文字是 CPU OpenCV，RGA 失败回退无统一性能/结果验证。 |
| 单 MPP H.264 Encoder | **PARTIAL / NOT_VERIFIED** | `dual_camera_pipeline.cpp:366-379,428-433`，`lib/mpp_encoder.cpp:322-437` | 只创建一个编码器并复制到自有 buffer；没有板端 SDK completion、PTS/EOS/flush 证据。 |
| 同一 packet fan-out | **PARTIAL** | `encoded_packet_fanout.h:11-19`，`lib/encoded_packet_fanout.cpp:15-49` | 两 sink 收到同一个 `shared_ptr` payload；同步回调、无独立队列和隔离背压。 |
| RTMP | **PARTIAL / NOT_VERIFIED** | `dual_camera_pipeline.cpp:122-169,411,418-422` | `popen(ffmpeg)` 路径存在；非空 `FILE*` 不等于服务器连接成功，未验证客户端画面。 |
| 分段 MP4 | **PARTIAL / NOT_VERIFIED** | `dual_camera_pipeline.cpp:134-142,423-426` | segment muxer 命令存在；未证明关键帧切换、trailer、PTS/DTS 和可解码分段。 |
| ISP Media Graph validator | **PARTIAL** | `lib/media_graph_validator.cpp:106-145` | 只读调用/离线文件读取存在；graph 只检查非空，不核对节点真实出现在 graph 中。 |
| rkAIQ/AE/AWB/曝光/增益 | **NOT_IMPLEMENTED / BLOCKED_BY_MISSING_BOARD_INFO** | `lib/aiq_controller.cpp:83-153` | 所有控制写入都返回 `Unsupported`；没有真实 SDK、Sensor、IQ 或回读。 |
| Sensor Driver/DTS/CSI/DPHY | **NOT_IMPLEMENTED / BLOCKED_BY_MISSING_BOARD_INFO** | 全仓库无 `.c/.h` Sensor subdev、`.dts/.dtsi` 或 BSP checkout；`docs/BSP_CAMERA_ISP.md:1-18` 明确说明缺失。 |
| 硬件曝光同步 | **NOT_IMPLEMENTED** | `dual_camera_pipeline.cpp:392`，`docs/HARDWARE_SYNC_FEASIBILITY.md` | 仅 fail-fast，未实现 FSIN/Trigger/master-slave。 |
| C++/CTest/ASAN/UBSAN | **NOT_VERIFIED** | 环境和命令记录见第 9 节 | 没有编译器和 CMake，不能用 Python PASS 代替。 |

## 4. 严重问题清单

### P0：0 个

本轮没有把“尚未拿到 BSP/硬件证据”自动升级为 P0。MPP 超时路径虽然缺少目标 SDK 完成语义，但源码在失败时保留 `pending_buffer_`，没有足够证据证明当前版本必然访问已释放输入，因此列为 P1 风险而不是已证实 P0。

### P1：11 个

#### P1-001：没有拒绝两个 Camera 使用同一设备

- 文件/行号：`lib/dual_camera_pipeline.cpp:395-407`。
- 触发条件：`--camera0 /dev/video0 --camera1 /dev/video0`，或两个不同路径实际解析到同一物理节点。
- 后果：两个 `V4L2IspCamera` 可能对同一设备分别 `open/start/DQBUF/STREAMOFF`，造成重复取帧、格式/stream 状态竞争和错误的双路声明。
- 证据：代码直接分别设置并启动，没有字符串相等检查，也没有调用 `media_graph_validator` 做实体/节点身份校验。
- 最小修复：至少拒绝 canonical path 相同的节点；板端再以 media graph entity/driver/physical path 校验不同物理源。
- 验证：相同节点、符号链接到同一节点、不同节点但同一实体的 mock/板端测试，均应在启动前失败。

#### P1-002：不同时钟域在无匹配候选时仍被直接比较

- 文件/行号：`lib/frame_pairer.cpp:61-67,146-157`。
- 触发条件：一侧 `Monotonic`、另一侧 `Realtime`，或任一侧 `Unknown`，且两队列同时有帧但没有合法候选。
- 后果：代码先正确地不把它们作为候选匹配，但随后用 `timestamp_less(a,b)` 的原始/normalized timestamp 决定丢哪一侧；这仍把不可比时钟域用于数据决策，可能稳定丢错帧。
- 证据：`compatible_clock_domains()` 返回 false 后，`timestamp_less()` 仍在 150、152 行被调用；上游 `lib/v4l2_isp_camera.cpp:650-655` 还把所有非 MONOTONIC flag 统一归为 REALTIME，UNKNOWN/COPY 可能先被错误标成可比较域。
- 最小修复：跨域只按显式策略丢弃并记录，不读取 timestamp 做大小比较；或要求外部提供已验证的时钟转换关系。
- 验证：Monotonic/Realtime、Unknown/Realtime、时间值相差数量级、队列持续满载测试。

#### P1-003：严格 AI 模式不是逐帧推理，异步模式没有最大结果年龄

- 文件/行号：`lib/dual_camera_pipeline.cpp:240-270,336-357`。
- 触发条件：采集 FPS 高于 `--ai-fps`，或 RKNN 推理慢于配帧速度。
- 后果：Strict 等待 `snapshot.sequence == frame_seq`，但 AI 队列按 `ai_fps` 抽样，很多配对帧永远没有对应结果，超时后无框；Async 只要求 `snapshot.timestamp <= frame_ts`，一条旧结果可以无限复用。
- 证据：没有 `max_result_age` 配置或比较；文档 `docs/CODEX_EXECUTION_STATUS.md:11` 宣称“过期结果不画”，源码并未实现。
- 最小修复：Strict 采用明确的每对帧推理/超时丢帧状态；Async 增加每路结果最大年龄、源帧 sequence/timestamp 和过期状态。
- 验证：AI 延迟 2 ms/10 ms/100 ms、丢帧、暂停推理、结果晚于合成帧，检查框和状态。

#### P1-004：两个 RKNN Context 都绑定 Core 0，CLI core 配置无效

- 文件/行号：`lib/dual_camera_pipeline.cpp:435-436`；`include/rknnPool.hpp:63-103`。
- 触发条件：任何启用 AI 的运行，尤其指定 `--npu-core0 0 --npu-core1 1`。
- 后果：两个 Context 不能按 Camera0/Camera1 分核；Core1 参数只是日志告警，吞吐和时延行为与命令行不一致。
- 证据：pipeline 对两个模型都传 `n=0`；wrapper 根据 `n` 调 `rknn_set_core_mask`，因此两者均为 `RKNN_NPU_CORE_0`。
- 最小修复：将 per-camera core 参数传入 wrapper，并在目标 RKNN SDK 上检查 `rknn_set_core_mask` 返回值和支持的 mask。
- 验证：启动日志、SDK query、两 Context 的 core mask 及双路推理压力测试。

#### P1-005：RKNN 模型契约和 NMS 逻辑未闭合

- 文件/行号：`include/rknnPool.hpp:125-209,268-281`；`lib/postprocess.cpp:114-147,345-381`；`dual_camera_pipeline.cpp:435`。
- 触发条件：NCHW 模型、多类别模型、输出 shape/anchors 与当前 YOLO 三尺度假设不一致，或出现不同类别的重叠框。
- 后果：输入始终标成 NHWC（191-209），即使 attr 是 NCHW 也可能送错布局；pipeline 固定 `class_num=1`；NMS 使用 `classIds[i]`/`classIds[i]` 条件，而不是排序后候选 `n/m` 的类别，可能跨类别错误抑制或漏抑制。
- 最小修复：按真实 attr 校验 input fmt/type/dims，拒绝不支持的模型；动态校验输出 shape/quant 参数和类别数，修正 NMS 类别索引。
- 验证：NCHW/NHWC、1/多类别、三尺度输出、量化边界和重叠框 golden tests。

#### P1-006：RTMP 和 MP4 没有独立消费者，任一路会阻塞整个编码链路

- 文件/行号：`lib/encoded_packet_fanout.cpp:15-49`；`lib/dual_camera_pipeline.cpp:122-169,413-426`。
- 触发条件：RTMP 网络阻塞、ffmpeg 子进程退出、磁盘写慢或 MP4 segment finalize 延迟。
- 后果：`publish()` 顺序调用 raw、RTMP、MP4 sink；`fwrite()`/`pclose()` 在 encode 线程同步执行，网络会阻塞录像，磁盘也会阻塞推流和后续采集。
- 最小修复：每个 sink 使用有界队列和独立消费者；定义满载策略、丢包/断线策略及终止状态。
- 验证：人工注入慢网络、停止 ffmpeg、只阻塞磁盘 sink，确认另一 sink 和 Capture/Pair 仍按策略运行。

#### P1-007：编码 PTS/DTS 没有进入 MPP 或实际封装器

- 文件/行号：`lib/dual_camera_pipeline.cpp:366-379`；`lib/mpp_encoder.cpp:367-375`；`dual_camera_pipeline.cpp:127-142`。
- 触发条件：配帧丢帧、输入 FPS 与实际 pair FPS 不同、MPP 内部有帧延迟或需要精确时间基的 MP4/RTMP 播放。
- 后果：应用只在 `EncodedPacket` 中自增 `pts`，未调用 `mpp_frame_set_pts`；ffmpeg 通过 `-framerate` 自行重建时间戳，应用的 `pts/dts` 没有被写入封装器。实际输出时间轴不能证明单调、连续且与配帧节奏一致。
- 最小修复：按目标 MPP API 设置 frame PTS，明确 timebase；使用真实 muxer API 或可验证的 ffmpeg timestamp 输入，记录 DTS/PTS。
- 验证：丢帧、变帧率、B-frame/无 B-frame、`ffprobe -show_packets` 和播放器检查。

#### P1-008：MPP flush/EOS 和完成语义没有目标 SDK 证据

- 文件/行号：`lib/mpp_encoder.cpp:322-437,477-518`。
- 触发条件：`encode_get_packet` 超时、非 timeout 错误、停止时编码器仍有延迟帧。
- 后果：超时后进入 faulted 并保留 buffer，但没有 BSP 级 reset/flush 证明；正常关闭直接 destroy context，可能丢掉编码器内部延迟帧。`pending_frame_` 也没有被赋值。
- 最小修复：按实际 Rockchip MPP 版本实现并验证 timeout、EOS、flush、packet ownership 和 context destroy 顺序；未验证前保持明确失败而不是报告正常输出。
- 验证：目标板注入 timeout/硬件错误，检查官方 completion、EOS trailer、最后一帧和 buffer 回收。

#### P1-009：ISP/rkAIQ/Sensor/DTS 核心要求没有实现

- 文件/行号：`lib/aiq_controller.cpp:83-153`；`docs/BSP_CAMERA_ISP.md:1-18`；全仓库无 Sensor/DTS/BSP 文件。
- 触发条件：任何需要 AE/AWB、曝光/增益控制、IQ 匹配、双路 CSI/DPHY 路由或硬件同步的板端运行。
- 后果：`query_status()` 只扫描进程/文件名并将 IQ 标为 `UNVERIFIED`；所有控制返回 `Unsupported`。没有真实 Sensor 型号、driver、DTS endpoint、rkAIQ 版本或 IQ 文件，无法证明 RAW→ISP→NV12 成像链路。
- 最小修复：取得实际 BSP/板卡资料后，按真实版本添加最小 driver/DTS/rkAIQ backend；控制失败必须继续 fail-closed。
- 验证：BSP 编译、`media-ctl -p`、`v4l2-ctl --all`、IQ 匹配、AE/AWB 回读、双路实帧和示波器/SOF 证据。

#### P1-010：RKNN wrapper 中存在高可信的目标编译错误

- 文件/行号：`include/rknnPool.hpp:297-302`。
- 触发条件：配置完整 RKNN/RGA/OpenCV SDK 并真正编译 `myDemo`。
- 后果：代码调用未限定的 `rectangle(...)`，该文件只写了 `using cv::Mat;`，没有 `using cv::rectangle` 或 `cv::rectangle`。在标准 OpenCV 头文件下很可能报“`rectangle` was not declared in this scope”，使目标主程序无法编译。
- 证据：全仓库未发现全局 `rectangle` 声明；当前主机无 C++ 编译器，因此只能标记为高可信静态缺陷，不能伪造编译结果。
- 最小修复：改为 `cv::rectangle` 并在真实 RKNN/OpenCV toolchain 上编译验证。
- 验证：执行完整 `cmake` configure/build，确认目标和链接均通过。

#### P1-011：双 AI 线程并发初始化共享的后处理全局状态

- 文件/行号：`lib/postprocess.cpp:29,287-299`；调用来自 `dual_camera_pipeline.cpp:286-307` 的两个 AI worker。
- 触发条件：Camera0/Camera1 首次推理同时进入 `post_process()`。
- 后果：静态 `init` 和 `labels[]` 没有 mutex/once 保护，两个线程可能同时加载/写入标签状态，形成 C++ 数据竞争，导致不确定的标签或未定义行为。
- 最小修复/验证：使用 `std::once_flag`/线程安全初始化，并用 ThreadSanitizer 或并发首帧压力验证。

### P2：11 个

#### P2-001：主机 C++ 测试没有编译主链路

- 文件/行号：`CMakeLists.txt:17-63`。
- 触发条件：在没有 vendor SDK 的普通主机上运行 advertised host tests。
- 后果：host targets 只编译 `app_options`、`composite_processor`、`frame_pairer`、`encoded_packet_fanout`、`media_graph_validator` 等小目标，不编译 `dual_camera_pipeline.cpp`、`mpp_encoder.cpp`、`v4l2_isp_camera.cpp`、`rknnPool.hpp`。Python PASS 不能发现主链路编译/ABI 错误。
- 最小修复：继续保留 vendor-free 单测，同时增加可编译的接口/mock target，或在实际 toolchain 上执行完整 `myDemo` 构建。
- 验证：CMake configure/build/CTest、交叉 toolchain 和目标 SDK 的真实链接。

#### P2-002：MPP packet/input 边界保护不完整

- 文件/行号：`lib/mpp_encoder.cpp:322-365,440-475`。
- 证据：`Encode()` 允许短于期望大小的 `yuv_size`，会把旧工作区尾部带入下一帧；输出 packet 缓冲在调用方固定为 `width*height*2`（`dual_camera_pipeline.cpp:367`）；`pkt_grp_` 初始化但没有用于 packet 分配。
- 后果：异常输入或极端码流时输出错误/截断，边界失败不够可观测。
- 最小修复/验证：精确检查输入大小、清零或拒绝短帧，动态扩大 packet 缓冲；用边界和高复杂度图像测试。

#### P2-003：Media Graph validator 的离线证据过弱

- 文件/行号：`lib/media_graph_validator.cpp:114-145`。
- 证据：`graph_available` 只要求文件/命令输出非空；没有确认 graph 中含有 `camera0_node`、`camera1_node`、Sensor→CSI→RKCIF/RKISP 路由或 endpoint。
- 后果：任意非空文本都可能通过 graph 可用性，不能作为真实拓扑验收。
- 最小修复/验证：解析 entity/link/video node 关系，使用保存的原始命令输出做负例测试。

#### P2-004：rkAIQ runtime/IQ 探测会把名字命中误报成 runtime

- 文件/行号：`lib/aiq_controller.cpp:28-40,83-99`。
- 证据：递归目录中只要文件名/目录名包含 `rkaiq` 或 `rkisp` 就标记 `vendor_runtime_detected=true`，没有库 ABI、版本或 Sensor/IQ 匹配检查。
- 后果：状态显示“runtime detected”但并不代表可初始化或可控制 ISP。
- 最小修复/验证：检查实际库、版本符号、IQ 文件和 backend ownership；使用伪造目录名负例。

#### P2-005：抓帧实验脚本与“只读”描述不完全一致，且没有 DQBUF 时间戳

- 文件/行号：`scripts/isp_capture_experiment.sh:44-48`、`scripts/analyze_isp_samples.py:63-77`。
- 证据：脚本可执行 `v4l2-ctl --set-fmt-video`，会改变节点格式；随后只保存 raw NV12 和说明文本，没有采集 V4L2 DQBUF timestamp/sequence。分析器的 exposure/gain 始终输出 `NOT_MEASURED`。
- 后果：不能用它证明曝光起点、同步时差或 AE 收敛。
- 最小修复/验证：把格式设置单独标成显式写操作，并由应用记录每帧 sequence/timestamp/flags。

#### P2-006：sink 重连没有退避、最大重试和重新发送头信息

- 文件/行号：`dual_camera_pipeline.cpp:147-156,161-168,429-433`。
- 证据：一次 `fwrite` 失败后立即 `pclose`、`popen` 并重试当前 packet；没有退避/最大重试，重连后不重新发送 SPS/PPS；`popen` 非空被当作启动成功。
- 后果：断线时可能反复 fork/阻塞，重连流缺少解码头；RTMP 状态无法区分“进程启动”“服务器连接”“客户端可见”。
- 最小修复/验证：独立 sink 状态机、退避/上限、重连时注入 codec headers，并用真实服务器/ffprobe/客户端日志验收。

#### P2-007：FramePairer 统计和测试覆盖仍不足以支持性能结论

- 文件/行号：`include/frame_pairer.h:58-75`、`lib/frame_pairer.cpp:15-27`、`tests/frame_pairer_test.cpp:21-80`。
- 证据：算法只记录 `abs(delta)`；没有序列跳变分类、每路等待时长分布或真实样本窗口。现有测试未覆盖固定 2/10 ms 偏差、一路长停、持续满载、停止时残帧和到达顺序组合。
- 后果：P50/P95/P99 只能在真正有配对样本时有意义；当前无真实样本，所有同步性能均 `NOT_MEASURED`。
- 最小修复/验证：补齐 10 个场景并在 C++ toolchain 执行，板端保存 pair CSV 后再计算统计。

#### P2-008：若干失败路径依赖析构，运行时可观察性不足

- 文件/行号：`lib/dual_camera_pipeline.cpp:407-435,447-450`。
- 证据：Camera 已启动后，协商不匹配、layout、raw 文件、ffmpeg、MPP header、AI required 等错误多处直接 `return false`；统一清理主要依赖 `DualCameraPipeline` 析构。编码失败时 pair queue 也可能仍持有 CameraFrame，随后 `camera.stop()` 先等待 3 秒。
- 后果：错误返回与资源释放之间存在延迟，`hold-last` 的 `last_` 引用也可能使 stop 超时；错误原因不一定对应当前还在占用的队列。
- 最小修复/验证：增加统一 cancel/drain/clear 顺序，先停生产者、排空 Pair/AI queue 和 `FramePairer`，再 stop camera；注入每个失败点检查 in-flight=0。

#### P2-009：模型标签路径硬编码且仓库不含标签文件

- 文件/行号：`lib/postprocess.cpp:26,97-101`。
- 证据：标签文件固定为 `../../model/coco_80_labels_list.txt`，当前仓库 `rg --files` 没有该路径；`loadLabelName()` 忽略 `readLines()` 返回值并始终返回 0。
- 后果：工作目录或部署布局变化时检测框可能没有类别文本；源码不能证明类别/置信度展示满足目标模型。
- 最小修复/验证：将 label path 做成模型/CLI 配置，严格检查文件和类别数，使用真实模型输出验证标签。

#### P2-010：FramePairer 的 delta 样本向量无界增长

- 文件/行号：`include/frame_pairer.h:70`、`lib/frame_pairer.cpp:78-86`。
- 证据：每个 matched pair 都 `push_back(delta_ns)`，没有窗口、上限或在线分位数。
- 后果：长时间 30/60 FPS 运行会让统计内存持续增长；无法作为稳定性证据。
- 最小修复/验证：使用固定窗口或在线 percentile，记录样本数和窗口策略；运行长稳测试检查 RSS。

#### P2-011：ISP 分析器对单样本时间戳报告样本数为 0

- 文件/行号：`scripts/analyze_isp_samples.py:65-73`。
- 触发条件：CSV 只有一行 timestamp。
- 后果：代码输出 `timestamp_samples=0`，与实际有 1 个样本不符；小样本统计无法严格审计。
- 最小修复/验证：显式区分“无行”和“1 行无 delta”，补充 0/1/2 行测试。

### P3：3 个

#### P3-001：文档与实现状态有过期/矛盾描述

- `docs/CODEX_EXECUTION_STATUS.md:11` 声称结果过期不画、`core-mask API` 未确认；但源码没有 max-age，且 wrapper 直接调用 `rknn_set_core_mask`。
- `README.md:3-5` 采用“主链路是两路真实 Sensor…”的架构陈述，实际只能证明用户态路径，不能证明真实 Sensor/ISP。
- 建议：把 `SOURCE_IMPLEMENTED` 拆成 source-present、host-built、board-tested，逐项绑定证据。

#### P3-002：兼容性字段和命名容易掩盖真实运行模式

- 文件/行号：`include/app_options.h:12-18`、`lib/app_options.cpp:97-103`。
- `device/width/height` 仍保留为旧别名，可能让单路参数看起来已支持，但双路字段才是实际入口。建议在 help 和报告中明确别名只映射 Camera0。

#### P3-003：缺少最终报告前的自动证据汇总

- 当前各文档分别记录 `NOT_MEASURED`，没有单一脚本汇总 Git、toolchain、BSP、模型、服务器和播放器状态。
- 建议把报告所需命令和退出码生成到一份不可编辑的审查日志，减少手工状态漂移。

## 5. FramePairer 正确性报告

### 源码算法

`FrameSample` 包含 camera_id、sequence、原始 timestamp、可选 normalized timestamp、clock domain、arrival_ns 和 `CameraFrame` lease（`include/frame_pairer.h:28-38`）。`push()` 用本地 monotonic arrival 只做等待计时（`lib/frame_pairer.cpp:197-222`），不会以线程 `clock_gettime()` 替换 V4L2 timestamp。两路队列最大深度由 `FramePairerConfig.max_queue_depth` 限制，默认构造时至少为 1（`30-36`）。

配对时遍历当前两队列，选 `abs(t0-t1) <= tolerance_ns` 中差值最小的候选，并丢弃选中项之前的旧项（`88-142`）。没有候选时会丢较旧项；但 146-157 对不兼容域仍调用 `timestamp_less()`，这是本轮 P1-002。

`tick()`/`expire_locked()` 依据 `arrival_ns` 和 `max_wait_ns` 丢弃等待帧；`hold-last` 会保留上一帧并生成 `StaleHoldLast`，不计入 `matched_pairs`（`162-194`）。`clear()` 能清空 pending queue，但当前 pipeline 的正常结束路径没有显式调用 `pairer.clear()`，而是依赖对象析构。

百分位采用线性插值 `(n-1)*p`（`15-27`）；空样本返回 0。这个数学实现可接受，但当前工程没有真实配对样本，不能把 0.0 或任何日志值写成板端 P50/P95/P99。

V4L2 timestamp 来源在 `lib/v4l2_isp_camera.cpp:640-655`：从 `v4l2_buffer.timestamp` 转为 ns，并保存 timestamp flags；只有设置 `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC` 才标记 Monotonic，否则统一标记 Realtime。代码没有把“unknown/driver-defined source”单独保留，也没有证明 timestamp 是 SOF、EOF 还是其他触发点。

### 要求场景和实际结果

| 场景 | 源码/测试状态 | 本轮实际结果 |
| --- | --- | --- |
| 完全同步 | `tests/frame_pairer_test.cpp:28-32` 有 3 ns 示例 | C++ 测试未执行；板端无样本，P50/P95/P99=`NOT_MEASURED` |
| 固定 2 ms 偏差 | 未覆盖 | 未执行 |
| 固定 10 ms 偏差 | 未覆盖 | 未执行 |
| 一路偶尔丢帧 | 部分由超时/丢旧逻辑表示 | 未执行 |
| 一路长时间停止 | `tick()` 有代码路径 | 未执行 |
| 时钟域不同 | `tests/frame_pairer_test.cpp:45-51` 只断言 reject 计数 | C++ 未执行；源码仍有跨域直接比较缺陷 |
| 时间戳乱序 | `tests/frame_pairer_test.cpp:45-49` 有案例 | 未执行 |
| 到达顺序变化 | 最近候选逻辑有实现 | 未执行 |
| 队列持续满载 | `tests/frame_pairer_test.cpp:59-64` 有深度断言 | 未执行 |
| 停止时残留帧 | `clear()` 有实现，pipeline 未显式调用 | 未执行 |

结论：可审计的软件算法骨架存在，但本轮没有实际运行 C++ FramePairer 测试，不能给出真实 P50/P95/P99，也不能把软件配帧写成硬件同步曝光。

## 6. 双路 AI 与 OSD 时序报告

### Context、输入和推理

- 两个 `std::unique_ptr<rknn_lite>` 和两个 AI worker 存在于 `dual_camera_pipeline.cpp:196,444`。
- 每路 capture 按 `ai_fps` 抽样，把带 camera 侧 sequence/timestamp 的 `AiItem` 入有界队列（`262-270`）；`ai_loop()` 先 copy NV12、RGA/OpenCV 转 BGR，再清空 CameraFrame lease 后调用同步 RKNN（`286-307`）。这使 Camera lease 不直接跨越 RKNN 调用，设计方向是正确的。
- `rknn_lite` 在 `rknnPool.hpp:191-209` 根据 attr 读取尺寸，但强制 `inputs[0].fmt=NHWC`；只接受一个输入和至少三个输出（`125-131`）。实际模型未提供，不能确认 RGB 顺序、letterbox、量化和输出 shape。
- `rknn_lite::interf()` 直接取 `outputs[0..2]`（`279-281`）；后处理固定 YOLO anchors/grid 和 pipeline 的 `class_num=1`。没有真实模型推理证据。

### 结果与当前合成帧的关系

每路 `AiSnapshot` 只保存 sequence、timestamp、boxes（`dual_camera_pipeline.cpp:179-180`）。合成时：

1. Strict 等待 `snapshot[id].sequence == frame_seq`，最多等待 `sync_max_wait_ms`（`336-341`）。
2. 读取 snapshot 后要求 `snapshot.timestamp_ns != 0 && snapshot.timestamp_ns <= frame_ts`；这只拒绝“结果来自未来”的情况（`342-345`）。
3. Async 没有最大年龄，任意旧结果只要 timestamp 小于当前帧就可继续画（P1-003）。
4. `map_box(view, box.box)` 使用同一 side-by-side scale/offset，再 `clip_box` 到输出尺寸（`346-353`；`composite_processor.cpp:108-122`），Camera0 左侧、Camera1 右侧偏移逻辑正确。
5. 状态文字显示 `AI_OK/AI_WAIT`、`age_ms`、`STALE` 和 pair delta（`354-362`），但“同步状态”是软件 pair 状态，不是曝光同步。

因此，“能画框”不能替代 AI 时间一致性：当前 async 可能画无限旧框，strict 在抽样推理下可能长期无框；两路 context 也没有按命令行核绑定。

## 7. RGA/CPU 拼接、OSD 与 NV12

`copy_nv12()` 在 `dual_camera_pipeline.cpp:48-75` 逐行去除 Y/UV stride padding，按 `data_length`/`data_uv_length` 检查可读范围，形成 tightly-packed NV12。V4L2 侧在 `lib/v4l2_isp_camera.cpp:587-696` 保存 plane offset、bytesused、sizeimage 和独立 UV 地址，这比历史版本完整。

运行路径 `dual_camera_pipeline.cpp:77-85` 先尝试 RGA `imcvtcolor`，失败时用 `cv::cvtColor`；`319-333` 先逐路 BGR，再尝试 RGA `imresize`，失败回退 `cv::resize`；`346-358` 的框和文字全部用 OpenCV CPU 绘制；`363` 再通过 `cv::cvtColor` 生成 NV12。源码因此不能描述为“RGA 硬件 OSD”，只能描述为“RGA 转换/缩放尝试 + CPU OSD/回退”。

`CompositeProcessor` 的 host CPU NV12 fallback（`lib/composite_processor.cpp:125-151`）只接受 tightly-packed 输入；它能验证左/右 UV 半区和几何映射，但没有真实 RGA stride、DMA-BUF import、cache/fence 或硬件结果证据。当前默认输出 1920×1080 左右布局，两个 1920×1080 输入会被等比例缩放到各自半区，Camera0 左、Camera1 右。

## 8. MPP、RTMP、MP4 实现审查

### MPP

`MppEncoder::EncodeNV12()`（`lib/mpp_encoder.cpp:248-307`）接受单一连续指针，stride 不同或 RGA 转换失败时逐行打包为 I420；上层已经先把 NV12/NV12M 转为 packed buffer，因此没有 NV12M 分离 plane 的零拷贝路径。`Encode()`（`322-437`）申请 MPP 输入 buffer、复制数据、`encode_put_frame()`，轮询 `encode_get_packet()` 最多 50 次，每次 1 ms。

源码没有 `mpp_frame_set_pts()`、EOS/flush 或明确的 packet completion 事件；停止时 `Release()` 直接 destroy context（`477-518`）。超时/错误时把 buffer 放入 `pending_buffer_` 并 faulted，这比立即释放安全，但仍没有目标 BSP API 证据证明 reset/flush/销毁边界正确。SPS/PPS 通过 `MPP_ENC_GET_EXTRA_INFO` 取出（`440-475`），但未做 Annex-B/AVCC 契约验证。

### packet fan-out

`EncodedPacket` 的 bytes 是 `shared_ptr<const vector<uint8_t>>`（`include/encoded_packet_fanout.h:14-19`），`encode_loop()` 只创建一次 payload 并发送同一个对象给所有 sink（`dual_camera_pipeline.cpp:374-376`）。这一点证明没有重复编码，但 `EncodedPacketDistributor::publish()` 在同一调用栈顺序执行每个 sink（`lib/encoded_packet_fanout.cpp:30-48`），没有每路队列、独立线程、丢弃/背压策略。

### RTMP/MP4

RTMP 命令是：

```text
ffmpeg -loglevel error -framerate FPS -f h264 -i - -c copy -f flv URL
```

MP4 命令是：

```text
ffmpeg -loglevel error -framerate FPS -f h264 -i - -c copy \
  -f segment -segment_time N -reset_timestamps 1 -strftime 1 \
  -segment_format mp4 dual-%Y%m%d-%H%M%S.mp4
```

两条路径都消费同一份 Annex-B 字节，并在启动时直接写 header（`dual_camera_pipeline.cpp:429-433`）。但当前实现没有：

- 等待 ffmpeg 子进程报告成功或 RTMP server handshake；
- 应用 PTS/DTS 到 ffmpeg/muxer 的传递；
- Annex-B→AVCC/extradata 的明确验证；
- 关键帧安全切段选项、segment trailer/close 检查；
- 重连退避、最大重试和重连后 SPS/PPS；
- `ffprobe`、播放器或 RTMP 客户端证据。

因此必须区分四个状态：代码路径存在、ffmpeg 进程创建、服务器连接、客户端看到含 OSD 的画面。本轮只有第一项得到源码证据。

## 9. ISP / Sensor / BSP 深度审查

### 当前能证实的内容

- `docs/BSP_CAMERA_ISP.md:1-18` 明确记载当前环境没有 RK3588 BSP checkout、Linux kernel tree、Sensor 资料、`/dev/media*`、`/dev/video*`、rkAIQ headers/libraries 或 IQ 文件。
- 全仓库没有 Sensor V4L2 subdev driver、probe/remove、I2C、runtime PM、mode table、RAW10/12、exposure/gain、`s_stream` 或 pad format 的新增实现。
- 全仓库没有板级 `.dts/.dtsi` patch，无法给出 Sensor I2C 地址、regulator、reset/pwdn、MCLK、endpoint、data-lanes、DPHY/CSI/RKCIF/RKISP 路由。
- `AiqController::query_status()` 只发现进程名和路径名，`set_ae_auto/set_exposure_us/set_gain/set_awb_auto()` 均在 `102-124` 返回 `Unsupported`；这符合 fail-closed，但不构成 rkAIQ SDK 集成。
- `media_graph_validator` 是只读外部命令/文件 validator，不改变 links、format、controls 或 stream；它不能替代实际 BSP 适配。
- `scripts/analyze_isp_samples.py` 只计算 NV12 Y 均值、标准差、最小/最大和可选 timestamp 间隔；曝光/增益始终 `NOT_MEASURED`。

### 能力标签

| 能力 | 标签 |
| --- | --- |
| 用户态 V4L2 NV12/NV12M 适配 | `SOURCE_IMPLEMENTED`，`BOARD_TESTED` 未验证 |
| Media Graph 只读探测 | `SOURCE_IMPLEMENTED`，`BOARD_TESTED` 未验证 |
| Sensor Driver | `NOT_IMPLEMENTED` + `BLOCKED_BY_MISSING_BOARD_INFO` |
| DTS/CSI/DPHY/RKCIF/RKISP 路由 | `NOT_IMPLEMENTED` + `BLOCKED_BY_MISSING_BOARD_INFO` |
| rkAIQ AE/AWB/曝光/增益控制 | `NOT_IMPLEMENTED` + `BLOCKED_BY_MISSING_BSP_SDK` |
| IQ 文件匹配/版本校验 | `NOT_IMPLEMENTED`，仅有 `UNVERIFIED` 状态字段 |
| AE 收敛/弱光/亮度实验工具 | 工具 `SOURCE_IMPLEMENTED`，真实参数和样本 `NOT_MEASURED` |
| 软件时间戳配帧 | `SOURCE_IMPLEMENTED`（存在 P1 跨域缺陷） |
| 硬件曝光同步 | `NOT_IMPLEMENTED` |

`--sync-mode hardware` 目前 fail-fast，不能标记为实现。没有 Sensor 数据手册、原理图、DTS/driver diff、FSIN/Trigger 线路和 SOF/示波器证据，就不能推断硬同步。

## 10. Buffer 生命周期、并发和停止顺序

当前正常路径的所有权时序如下：

```mermaid
sequenceDiagram
    participant D as V4L2 driver
    participant C as Capture0/1
    participant P as FramePairer
    participant Q as Pair/AI queues
    participant A as AI worker
    participant R as Compose/OSD
    participant M as MPP encoder
    participant F as RTMP/MP4 sinks

    C->>D: VIDIOC_DQBUF(index, timestamp, bytesused, offset)
    D-->>C: mmap plane pointers + metadata
    C->>C: lease_state.acquire(index), slot=InUse
    C->>P: FrameSample(frame copy, camera_id, seq, timestamp)
    C->>Q: optional AiItem(frame copy)
    P->>Q: FramePair(cam0, cam1)
    Q->>R: pop pair
    R->>R: copy each NV12 to owned packed buffer
    R->>R: RGA/OpenCV conversion + CPU OSD + BGR->NV12
    R->>M: EncodeNV12(encoder-owned input buffer)
    M-->>R: encoded Annex-B packet
    R->>F: one shared EncodedPacket payload
    A->>A: copy NV12 -> BGR -> RKNN; reset AiItem.frame
    A-->>R: ResultStore(camera_id implicit by slot, seq, timestamp, boxes)
    Note over C,D: last CameraFrame lease release -> QBUF
    C->>D: stop_accepting(); wait_for_zero(); STREAMOFF
    C->>D: munmap/close after consumers are gone
```

释放条件和缺口：

1. `BufferLeaseState::release()` 在正常运行中最后一个引用释放时调用 QBUF（`include/camera_frame.h:87-114`）；停止后 `stop_accepting()` 会禁止新的 QBUF，改由 STREAMOFF 回收。
2. Pair/AI queue 丢旧时，`CameraFrame` shared lease 的析构会释放引用；但编码失败时 `pair_queue` 可能仍有未消费帧，`run()` 先调用 `camera.stop()`，会等待到超时后再依赖析构释放（`dual_camera_pipeline.cpp:447-450`）。
3. AI 在 copy NV12 后清空 `item.frame`，因此 RKNN 不持有 V4L2 mmap；这是当前最清楚的安全边界。
4. RGA/CPU 处理使用独立 `std::vector`/`cv::Mat`，不直接把 Camera mmap 交给 MPP；但 RGA DMA-BUF import/fence 没有实现。
5. MPP 的输入是自有 `MppBuffer`；成功收到 packet 后释放，timeout/error 时保留到 context destroy。完成语义尚未按目标 SDK 验证。
6. packet bytes 在同步 sink 回调全部返回前由 `shared_ptr` 保活；没有异步消费者，因此“共享引用”不等于“隔离背压”。
7. `hold-last` 会把上一帧保存在 `FramePairer::last_`；pipeline 结束前没有显式 `clear()`，该模式更容易让 camera stop 等待到超时。

## 11. 构建、工具和实际测试记录

### 环境探测

```text
Python 3.12.7                         available
Git 2.52.0.windows.1                  available
FFmpeg 7.0.2 / ffprobe                available
cmake / ctest                         MISSING
g++ / gcc / clang++ / cl              MISSING
ninja / make / clang-tidy / cppcheck  MISSING
v4l2-ctl / media-ctl                  MISSING
../rknn / ../mpp / ../rga / ../opencv4-1  MISSING
RK3588 / /dev/video* /dev/media*      unavailable on Windows host
```

### 实际命令和结果

| 命令 | 结果 |
| --- | --- |
| `git status --short` | exit 0；工作区非空，12 modified + 31 个 untracked entries（目录折叠后）；展开文件列表为 47 个 untracked files |
| `git log -5 --oneline` | exit 0；HEAD=`6085dab` |
| `git diff --stat` | exit 0；仅统计 tracked 工作区改动，351 insertions / 224 deletions |
| `git diff --check` | exit 0；无 whitespace error |
| `python tests/validate_project.py` | exit 0，`validate_project: PASS (32 required files)` |
| `python -m compileall -q tests scripts` | exit 0 |
| `cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON -DBUILD_BSP_TOOLS=ON` | exit 1；`cmake` not recognized，未生成 build directory |
| `cmake --build build-host -j` | 未执行成功；没有 cmake |
| `ctest --test-dir build-host --output-on-failure` | 未执行成功；没有 ctest |
| `bash -n scripts/*.sh` | 未执行；Windows sandbox 拒绝启动 bash（ACCESS_DENIED） |
| ASAN/UBSAN/static analyzers | 未执行；无 C/C++ 编译器及工具 |
| RK3588 V4L2/MPP/RGA/RKNN | 未执行；无设备/SDK/模型 |

### 已运行的独立 Python/FFmpeg 检查

1. 用 4×4、24-byte synthetic NV12 和三条 timestamp 运行：

   ```text
   python scripts/analyze_isp_samples.py --nv12 sample.nv12 --width 4 --height 4 --timestamps ts.csv
   exit 0
   y_mean=23.5000 y_stddev=4.6098 y_min=16 y_max=31
   timestamp_samples=3 delta_ms_p50=33.333333 delta_ms_p95=33.333333
   exposure_us=NOT_MEASURED gain=NOT_MEASURED
   ```

2. 对同一分析器传入奇数宽度，得到参数校验 `exit 2`；对只有一行 timestamp 的 CSV 实测 `exit 0` 但输出 `timestamp_samples=0`，确认了 P2-011；这些只证明脚本边界检查。

3. 用系统 FFmpeg 生成 64×64 synthetic H.264，再使用与项目相同的 raw H.264 segment 方式生成 MP4，并用 ffprobe 检查：生成、分段、ffprobe 均 `exit 0`，得到 `codec_name=h264 width=64 height=64 duration=1.000000`。这只证明本机 FFmpeg 的独立 muxer 能工作，**不证明项目 MPP packet、PTS、RTMP 或项目生成的分段可解码**。

### 未能运行的源码测试

`tests/frame_pairer_test.cpp`、`tests/composite_processor_test.cpp`、`tests/encoded_packet_fanout_test.cpp`、`tests/media_graph_validator_test.cpp`、`tests/camera_lifecycle_test.cpp` 都需要 C++ 编译；本轮没有把它们误报为 PASS。现有 FramePairer 测试只覆盖 3 ns 示例、容差丢旧、乱序、域拒绝、超时、队列深度和最近候选，没有覆盖用户要求的完整十组同步场景。

## 12. 后续最小改动清单

### P0

当前没有已证实 P0。若目标 MPP SDK 实测证明 timeout/error 后硬件仍访问已释放资源，应立即升级为 P0 并停止发布。

### P1

1. 启动前拒绝相同节点/同一物理实体，补 media graph identity 检查。
2. 修复 FramePairer 跨时钟域丢弃逻辑，禁止用不可比 timestamp 决定顺序。
3. 为 AI 增加逐帧 strict 语义、每路最大结果年龄和明确的 stale/late 状态。
4. 传递 per-camera RKNN core 配置，验证真实 SDK mask；按模型 attr 处理 NCHW/NHWC、类别数、输出 shape 和 NMS。
5. 将 RTMP、MP4 改成独立有界消费者；定义断线/磁盘满/重连策略。
6. 把 PTS/timebase 真实送入 MPP/muxer，验证 SPS/PPS、Annex-B/AVCC、关键帧切段和 DTS 单调性。
7. 按目标 MPP 版本验证 EOS/flush/timeout/completion，停止时排空或明确丢弃延迟帧。
8. 获取真实 BSP/两颗 Sensor/板级资料后实现最小 Sensor/DTS/rkAIQ backend；在资料缺失时保持 fail-closed。

### P2

1. 增加主链路可编译的 host mock target，并在真实交叉 toolchain 上运行完整 `myDemo`。
2. 收紧 MPP input/packet size、slot 状态、重复 DQBUF/QBUF 和 stream stop 失败的测试。
3. 让 Media Graph validator 解析 entity/link/node，而不是仅检查非空输出。
4. 改进 runtime/IQ 版本和 ownership 探测，抓帧脚本明确区分写 format 与只读采样。
5. 补齐固定 2/10 ms、长停、乱序、持续满载、停止残帧、慢 AI、慢 sink、MPP timeout、异常退出等自动化用例。
6. 给 sink 重连增加退避、上限、header 重发和可观测状态；运行时记录 sink queue depth、drop、PTS/DTS 和 ffmpeg exit status。

### P3

1. 重写过期的 `SOURCE_IMPLEMENTED` 文档，逐项区分 source/host/target/board/measured。
2. 在 CLI help 中明确旧 `--device/--width/--height` 只映射 Camera0。
3. 把本轮审查命令和退出码纳入可重复的 evidence script。

## 13. 面试官视角评价

### Camera Driver

可以写：实现/设计了 V4L2 capture、NV12/NV12M plane/stride/bytesused 校验、MMAP lease、poll/DQBUF/QBUF/STREAMOFF 和双节点参数化。不能写：已在具体 RK3588 双 Sensor 上稳定采集，或已完成硬件同步曝光。

### ISP 开发

可以写：理解 RK3588 CSI/RKCIF/RKISP/rkAIQ 的边界，提供只读 Media Graph/运行时探测和 fail-closed 控制接口。不能写：完成 Sensor driver、DTS、IQ、AE/AWB、曝光/增益回读或 ISP 成像调参。

### Linux Multimedia

可以写：实现了单路合成帧的 MPP H.264 源码路径、RGA/CPU 转换、共享编码 packet 和 FFmpeg sink 原型。不能写：已完成零拷贝、硬件编码 flush/PTS 验证、可解码分段 MP4、RTMP 直播或 30/60 分钟稳定运行。

### 嵌入式 C++

可以写：使用 C++17、RAII/shared ownership、条件变量、有界队列和错误传播组织双路 pipeline。不能写：已通过 C++ 构建、CTest、ASAN/UBSAN、目标板回归或长稳测试。

审查结论中的 P0/P1/P2/P3 数量为：**0 / 11 / 11 / 3**。最严重的五个问题是：

1. 跨时钟域仍用原始 timestamp 决定丢帧；
2. Strict/Async AI 没有正确的逐帧/最大年龄契约；
3. 两个 RKNN Context 都绑定 Core 0，且模型输入/NMS 契约未验证；
4. RTMP/MP4 同步 sink 会互相阻塞，不能提供独立故障隔离；
5. MPP PTS/EOS/flush 和 ISP/rkAIQ/BSP 关键路径都没有真实目标证据。

下一轮最值得先修复的三个问题：

1. 先修 FramePairer 跨域和 AI 结果年龄/strict 语义；
2. 再把 packet fan-out 改成独立有界消费者并完成 MPP PTS/EOS/flush 验证；
3. 最后在拿到真实 BSP/硬件资料后补 Sensor/DTS/rkAIQ 最小 backend 和板端证据。

