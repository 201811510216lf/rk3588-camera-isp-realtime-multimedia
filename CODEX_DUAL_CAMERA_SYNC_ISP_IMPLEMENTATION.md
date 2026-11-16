# Codex 执行规范：RK3588 双路 MIPI RAW / ISP / 同步配帧 / AI 标注 / 拼接推流

> **文档属性：双路需求与实施计划；范围说明更新于 2026-11-16。**
>
> 正文描述目标和验收要求，不是已完成功能清单，也不构成板端验证证据。当前代码还包含独立的 VirtualSensor / Software ISP 主机路径；板端双路 V4L2、硬件同步、RKNN/RGA/MPP 和 RTMP/MP4 的实际实现及验证边界以现行架构和状态记录为准。
>
> 现行依据：[当前架构](docs/CURRENT_ARCHITECTURE.md) · [执行与验证状态](docs/CODEX_EXECUTION_STATUS.md)。范围说明更新：2026-11-16（Asia/Shanghai）。

> 适用仓库：`rk3588-camera-isp-realtime-multimedia`。**必须在现有仓库增量改造，禁止重建项目**；仅在审计证明现有模块不可复用时替换局部。原先独立的四路 RTSP/四模型/四宫格业务不得重新引入。
>
> 业务目标：实验动物顶部 + 侧面双视角监测。两路 RAW Sensor 经 RK3588 BSP 的 CSI/RKCIF/RKISP/rkAIQ 输出，经 V4L2 双路采集，形成时间匹配的帧对，分别运行或共享 RKNN 推理，将检测框叠加到对应路图像，RGA 左右拼接为**一路同步双视角视频**，只编码一次，以 RTMP 实时推流并同时分段 MP4 录像。JSONL 为可选调试记录。
>
> 硬件条件：用户拥有 RK3588 开发板、两颗 MIPI RAW Camera、完整 BSP 源码。**但 Codex 运行环境是否能访问板子/BSP 仍需实际检测。** 不得因为无法 SSH 就不写 BSP/ISP 相关的软件代码；不得将未接触到的驱动、设备树、rkAIQ 版本猜测为已知。

## 0. 总原则和验收定义

1. 源码主战场是现有 `main.cpp`、V4L2 Camera、CameraFrame/lease、MipiPipeline、MPP Encoder、RGA、RKNN、队列和 CMake。根据实际结构进行最小文件级修改。先审计旧报告 `docs/CODE_REVIEW_REPORT.md`、`docs/FINAL_ACCEPTANCE_REVIEW.md`（若有）及新提交；保留既有可用实现。
2. **必须交付可编译的软件实现**，包括双路 capture、FramePairer、OSD/Compose、RTMP/MP4、ISP BSP 适配代码；编译依赖不齐时将通用代码与 SDK 适配分离并完成所有能执行的单测，不允许用空 stub 或假返回成功代替真实关键路径。
3. **硬件真实可用与代码实现分开认定**。证据状态：`SOURCE_IMPLEMENTED`、`HOST_BUILT`、`HOST_TESTED`、`TARGET_BUILT`、`BOARD_TESTED`、`MEASURED`；单项无证据不得越级。不能 SSH 时可交付待应用 BSP patches + 完整验证脚本，但 `BOARD_TESTED` 不得为 true。
4. 每一步均必须持续执行：审计 → 实际编码 → 编译/测试 → 修复 → 记录 → 下一步；不需要阶段确认，除非真的缺少不可推断的板卡端口/Sensor 型号而无法安全修改对应硬件 DTS，此时继续实现其余所有模块并列出精准采集命令。
5. **严禁声称两个 ISP/VPU/NPU 核能够使两颗 Sensor 同时曝光**。处理并行与 Sensor 曝光同步是不同问题。

## 1. 核心产品规格与输出

- 两路独立真实 MIPI RAW Sensor，`cam0` 顶部、`cam1` 侧面；分别对应经 Media Controller 验证的两个 ISP NV12/NV12M video 节点。禁止用同节点/视频文件复制伪造双路。
- 两路帧分别携带 `camera_id`、`sequence`、`timestamp_ns`、`clock_domain`、`timestamp_source_flags`、宽高、stride、plane offset/bytesused/sizeimage、lease。各路 capture 独立线程。
- 提供 `--sync-mode software|hardware`（hardware 仅真正完成硬件触发配置且验证后启用，未支持时返回明确错误），`--sync-tolerance-ms`、`--sync-max-wait-ms`、`--sync-policy drop-older|hold-last`；默认 `software` + `drop-older`，严格同步测试禁止 hold-last 冒充同一时刻画面。
- 关键交付是**一个带双路图像和对应检测框的拼接后视频流**。默认缩放两个 1920x1080 输入到各 960x1080（保留比例需按 letterbox/crop 策略明确），形成 1920x1080 并排；支持 3840x1080 高分辨率配置，但不假定 MPP 一定能达到目标 FPS。
- 默认为单路输出 H.264；编码一次将拥有独立内存的 packet fan-out 到 RTMP publisher 和分段 MP4 muxer。RTMP 必须实际接入已知服务器，MP4 需可由 ffprobe/播放器打开；可选 JSONL 记录配帧时差/AI 事件，不是唯一输出。
- 支持 `--ai-mode off|async|strict`：`async` 默认，在每路画面上标注**对应 camera_id 的最近有效结果**，不会跨相机混用，也不得使用采集时间在待显示帧之后的结果；`strict` 需等待对应帧结果，支持超时/丢帧，不承诺 30FPS。
- 支持 `--model` 指向一个共享模型文件、两个独立 RKNN Context；`--model0/--model1` 可选，用于模型经实验证明需要按视角区分时。基于 RKNN Runtime 能力与实测分别配置 `--npu-core0`、`--npu-core1`，不可在一个非线程安全 Context 上无锁并发；先单模型双 Context，禁止为了“两个核”机械训练两个模型。
- 标注 `cam0/cam1`、时间差 `delta_ms`、帧序号、AI 结果年龄、框/类别/置信度；如果超时或丢帧，输出明确的诊断计数，不能悄悄用旧帧标成“同步成功”。

## 2. 三个层级的同步能力：必须独立实现和测量

### S1：同一合成视频帧（软件配帧）——必须完成

- 新增 `FramePairer`，每路有界时间戳序列缓冲，保证配帧逻辑和 RGA/编码解耦。仅在时间戳**同一时钟域且相同时间戳定义**时直接比较，否则拒绝精确同步声明；如有可证明转换关系再规范化。
- 采用 `abs(t0-t1) <= tolerance_ns` 作为基础候选门限，遇到多个候选时选择差值最小的单调匹配，不重复消费同一帧；同步判断须考虑交付延迟、最大等待和旧帧丢弃策略。
- 不允许无界等待新帧。`drop-older` 应丢弃无法匹配的较老帧并正确释放 lease；`hold-last` 仅是面向展示的降级模式，视频上必须明显标 `STALE/UNSYNCED`，不计入真实配帧成功。
- 每一个输出 `FramePair` 含两路 source sequence、raw timestamp、normalized timestamp、`delta_ns`、配对状态与 pair_id；记录 P50/P95/P99 `abs(delta)`、配帧成功率、两路 source drop、pair drop、最大队列深度。
- 提供 synthetic timestamps 和 out-of-order、丢帧、相机速率不一致、时间戳突变、关闭时 lease 释放的单测；通过模拟测试只能证明算法，不证明真实曝光同步。

### S2：基于内核采集时间戳的近时刻匹配——必须提供板端测量途径

- 查询 V4L2 `timestamp flags`，核对文档对应时间戳触发点（SOF/EOF/其他），并记录 Sensor 帧序号、采集队列延迟、曝光时长/rolling shutter 情况；不可仅从 DQBUF 线程的 `clock_gettime()` 值声称对应曝光起点。
- 提供可运行的双路原始帧抓取与 `--sync-diagnostics` 日志/CSV，分析同一帧配对时差分布；必要时考虑设备/驱动可提供的 SOF metadata 或更准确的硬件时间源。
- 软件启动双路 `STREAMON` 顺序不可当作同步控制；两路 30FPS 仅表示名义帧周期一致，存在相位和时钟漂移。

### S3：Sensor 硬件曝光同步——有条件必须实际编写 BSP 方案

- 审计**具体两颗 Sensor 数据手册与 BSP 驱动**是否有 FSIN/trigger/master-slave 外部同步、frame sync 寄存器、GPIO 接线/clock 源、模式限制。`--sync-mode hardware` 仅当硬件明确支持且 BSP 初始化成功时可用。
- 若支持且物理线路可用：在 BSP 内按现有风格修改 Sensor V4L2 Subdev 驱动/controls、DTS 的 pinctrl/GPIO/endpoint、必要的 trigger 配置与启动顺序；在代码中提供可执行状态检测、错误恢复、模式切换和 `get status`，尽可能配置共同曝光时间与帧长。注意 rolling shutter、曝光长度与不同 Sensor 型号的时间偏差。
- 若 Sensor/走线不支持：**仍交付完整 S1+S2 软件实现**，生成 `docs/HARDWARE_SYNC_FEASIBILITY.md` 详细说明实际缺少的引脚/功能，`hardware` 模式 fail-fast，不能假写寄存器或编造硬同步。
- 如果需要 MCU/PWM/外部触发，只有确定有连线且脉冲电平/时序满足手册才启用；不得直接在未核实 GPIO 上输出波形。

## 3. ISP / Sensor / BSP：必须交付真实代码，而非仅文档

### I0 BSP 源码识别与变更边界

- 探索 BSP 版本、当前 Sensor driver、板级 DTS/DTSI、rkisp/rkcif、rkAIQ 头文件和动态库及其用户态进程，识别 IQ 文件路径和两路链路；准确列出 Sensor 型号、I2C 地址、clock/reset、MIPI lanes、对应 video/media/subdev 节点。
- 若 BSP 目录可访问且支持 git：**直接最小化修改并单独 commit/patch**；不要把完整 Linux kernel 复制到应用仓库。若 BSP 不可访问，将软件适配分成 `bsp_patches/` 下的与实际 BSP 版本匹配的 `.patch`，而非猜测 DTS。只允许基于读到的真实源码制作能够应用的 patch。

### I1 Sensor driver + DTS + Media Graph

- 复用 BSP 现有 Sensor 驱动，不要求重新造传感器算法。**代码审计后**为两颗实际模块补齐/修正真实需要的 probe、I2C ID、regulator、reset/pwdn、MCLK、clock frequency、format/mode table、gain/exposure controls、stream_on/off、frame interval、Runtime PM/错误处理。
- 补齐/修正对应板级 DTS/DTSI：两个独立 Sensor 节点，MIPI DPHY/CSI endpoint 的 remote-endpoint、data-lanes、路由到 rkcif/rkisp 的 sditf/virtual pipeline、必要 pinctrl 和供电；若硬件实际采用共享 ISP/readback，不得伪称两个独立 ISP。
- 应用提供**可运行**的 `MediaGraphProbe`/`CameraTopologyValidator`（复用 `media-ctl`/sysfs/v4l2 API 可行方式）校验 Sensor entity 名、links、video node 对应关系、格式、Bayer pattern、FPS 及两路是否独立；探测失败默认拒绝宣称 ISP 模式正常。
- 提供板端构建/安装 DTS dtb/module 的准确命令及回滚步骤（以真实 BSP 路径和构建系统为依据），勿凭空用通用命令写死。

### I2 rkAIQ 控制层：源码必须实现

- 在用户态增加最小 `AiqController` 适配层：依据**实际 BSP 的 rkAIQ API 版本**实现每个 Sensor 的 `query status`、`AE auto/manual`、`exposure time`、`analog/digital gain`、`AWB mode`（若 API 支持）、当前 IQ 路径/版本可诊断输出、失败码和运行日志。
- 先检测已有 rkAIQ daemon/service 对 ISP 的所有权；如果服务已管理该 ISP，优先使用该 BSP 支持的控制接口，**不可同一 ISP 再次 init 竞争所有权**；若 BSP 仅支持本进程管理，在明确生命周期下调用官方 ctx init/prepare/start/stop/deinit。
- `IspControlCli` 至少提供 `--list-cameras`、`--isp-status`、`--ae-auto`、`--exposure-us`、`--gain`、`--awb-auto`、`--dump-isp-state`（可按现有 CLI 设计）；应用参数必须真实调用 SDK/可验证服务，而不是只回显值；不支持项明确报错并在文档中说明。
- 实现 `AE/AWB` 采样工具，按 `cam_id, capture_ts, seq, exposure_us, gain, wb_mode, brightness` 保存 CSV；支持光照突变下的收敛评估。不可把简单参数控制描述成自己实现了 3A 算法。
- IQ 文件要做路径、Sensor/镜头版本匹配与加载状态检查；调参需备份/回滚；不得凭空生成能工作的厂商 IQ 文件。若版本/SDK 未提供某能力，保留清楚编译期开关和拒绝运行行为，不能用假正常返回凑齐接口。

### I3 ISP 图像质量实验基础

- 支持抓取 `RAW`（仅 BSP/硬件支持时）和 ISP 输出 NV12 样本；至少记录 AE 收敛时间、固定曝光对比、亮度/噪声统计及时间戳；测试与算法评估结果必须来自真实帧。
- 提供真实的实验脚本、对照参数、CSV 格式及数据分析代码；没有数据时图表标 `NOT_MEASURED`，不使用伪造数据填报告。

## 4. 视频处理实际数据链路——优先修改当前代码

```text
RAW Sensor 0 → MIPI0 → verified ISP route → V4L2 capture0 ─┐
                                                           ├→ Timestamp FramePairer → PairedFrame
RAW Sensor 1 → MIPI1 → verified ISP route → V4L2 capture1 ─┘                │
                                        ┌───────────────────────────────────┤
                                        │                                   │
                              per-camera RGA preproc               Overlay/Compose worker
                                        │                                   ↑
                      RKNN context0 / RKNN context1                 per-camera latest detections
                                        │                                   │
                          YOLO dequant/NMS + cam_id ────────────────┘
                                                                            │
                                                   RGA side-by-side NV12 composite + OSD
                                                                            │
                                                          ONE MPP H.264 encoder
                                                                            │
                                                       OwnedEncodedPacketDistributor
                                                                     ├→ RTMP (one composite URL)
                                                                     └→ segmented MP4 (same composite)
```

- AI 可以独立由各 Capture 分支投递，不必等待配帧完成；所有推理结果必须与相应 `cam_id` 和源帧 TS 对齐后才叠加。
- `FramePairer` 的作用是组成画面帧对；**不是** NPU/VPU 同步器。一个输出帧必须来自一个且仅一个 pair_id。
- 编码前仅调用一次合成/OSD 处理并使用一个 MPP 编码实例（可配置高级模式单路分编码用于诊断，但不得作为默认主链路）；输出一条 RTMP URL，不再默认两路独立 URL。
- 对两幅图的 aspect ratio、scale、letterbox、crop、ROI 与 AI 框坐标变换要使用相同几何映射，提供矩阵/坐标测试。视频输出可选 1920×1080/3840×1080，默认按能力选择并记录实际编码规格。
- OSD 绘制使用适合 RGA/NV12 的路径，或在独立可写中间缓冲区通过 OpenCV/BGR 绘制后再转换；严格记录 copy 数量；不直接写 V4L2 驱动复用的原始缓冲。给出颜色格式转换与 stride 校验。
- 未启用 AI 时仍能同步双路拼接/推流/录像；模型失败时应可降级为无 AI 并明确日志与状态。
- 某路失联时默认中断**严格同步拼接**并报错/重连；可配置降级为只显示另一画面，但必须显示 `CAMERA OFFLINE`，不能冒充双路同步成功。

## 5. 性能与背压、所有权（任何阶段优先安全）

- 先修正旧审查 P0/P1：MPP 输入 buffer 完成语义、V4L2 plane offset/sizeimage/bytesused/timestamp、poll 错误、异常 index、STREAMOFF 清理、Camera lease、RKNN 模型输入数量、SPS/PPS 错误传播等；已经被删除的 Legacy 独有问题可记 `REMOVED`。
- Capture、Pair、AI、Compose/OSD、Encode、Publisher、Recorder 各有明确 thread/worker 或事件驱动状态机；不是强制一个模块一个线程，强调阻塞不传播与所有权。队列容量有上限，分别给出满队列策略。
- `CamFrame` lease 只能在 RGA/MPP 已完成访问之后回 V4L2；`shared_ptr` 析构不等于硬件 DMA 完成；若 MPP 输入位于独立工作缓冲，可在 copy 完成后及时 QBUF；RGA fence/sync 以本 BSP API 为准。
- `FramePairer` 控制最多在途帧数，以免等待另一路时耗尽 Camera buffer；AI 优先预处理复制到独立内存再释放 Camera lease。
- RTMP/MP4 对编码包各自有界消费；网络阻塞不导致录像阻塞，磁盘满不无期限卡住编码；单个 encoder 输出的 PTS/DTS 必须单调，帧率以配帧输出的实际节奏为准。
- NPU worker 模型 core mask 在 SDK 支持时分别设置核 0/1，对比 AUTO/固定绑定；NPU 只影响推理吞吐与时延，不能改变原始帧采集时间。根据模型及软硬件约束才启用共享 core 或并行模型实例。
- 默认单路 VPU 编码拼接帧。确需双路分编码才引入两个 encoder context 并基于 SDK 验证能力；不能仅因硬件有两个 VPU 就使用两个 encoder。

## 6. 工程执行阶段及硬门槛

**E0：源码审计/编译基线**
- 检查现有仓库、历史报告、CMake、线程和实际完成状态、BSP/板卡支持性。建立构建基线、回归命令，列出最小文件改动清单。
- 完成标志：源文件/函数级审计结果和基线测试。**不要只写方案。**

**E1：基础正确性修复**
- 修复确认的旧 P0/P1 缺陷；补充 V4L2 layout/lease/encoder error unit tests，执行可用测试。
- 完成标志：已修缺陷有源码和测试证据；硬件完成语义如尚无法验证明确标识且保证保守 copy 路径。

**E2：两路采集/同步配帧/拼接输出（第一展示里程碑）**
- 将现有 `MipiPipeline` 增量调整为双 Camera Capture；实现有界 `FramePairer`、RGA side-by-side NV12 Compose、一路 MPP encoder；初期允许禁用 AI 先确保实时双路拼接视频成型。
- 完成标志：host mock 两路帧能按时间戳配对/拒绝过期帧；真实板端若可访问则两路真正采集并可播放拼接输出；运行时输出 delta_ms 和 pair metrics。

**E3：BSP/Sensor/DTS/CSI/ISP 软件实现（必须写代码）**
- 依据真实 BSP/模组源码新增或修改 Sensor driver、板级 DTS、Media graph validator、rkAIQ adapter/control CLI、状态日志与 AE/AWB 实验采集工具；无硬件连接也必须实现能实现的真实软件部分。无法得到真实硬件引脚、地址和 SDK API 时先探测/生成不含虚构事实的版本适配补丁清单，不能伪造补丁。
- 完成标志：明确真实代码 diff、目标 BSP 适配与编译情况；硬件测试另记。

**E4：RKNN 双路结果与 OSD 同屏（第二展示里程碑）**
- 两个 RKNN context 可加载同一个模型文件；core mask 可配；每路单独 preprocess/YOLO/NMS/ResultStore，配帧标注坐标、过期检测、strict/async 模式；在左右画面显示两个相机各自检测结果。
- 完成标志：host synthetic boxes+composite 测试，板端可视化验证；不存在错位、跨路串框、无期限旧框。

**E5：直播 + 录像同步输出**
- 从旧仓库选择性移植已经审计的 FFmpeg/RTMP publish 模块，仅用于**单路合成编码**，实现网络重连与正确 H.264 timebase/codec headers；同一 packet fan-out 到分段 MP4；给出 ffprobe/ffplay/服务器拉流验证。
- 完成标志：播放器可以看到同时显示两路且含双路 AI 框的单一路实时画面、MP4 同内容可播放；没有测试时必须明确 AWAITING_HARDWARE/SERVER。

**E6：同步能力深化 + 性能优化**
- S1 配对统计完整；S2 真实 V4L2 timestamp 校验、时钟域和相机曝光/行时间的分析；S3 有 FSIN 等实际支持则实现 BSP trigger/模式并测试，否则输出真实不支持原因且硬件同步模式 fail-fast。
- DMA-BUF import/RGA/MPP 只有确认真实 SDK、cache/stride/fence 才启用；保留保守安全复制路径，对比 CPU/RSS、sync P50/P95/P99、capture FPS、pair FPS、encode FPS、AI FPS、端到端 P95、Buffer in-flight、掉帧分类。
- 完成标志：若板端可用则有单路/双路对比与 30/60 min 稳定性；若不可用则齐全的采集脚本/CSV 规范/板端验收表，而不是伪造结果。

## 7. 必须生成的测试与交付

- `docs/CODEX_EXECUTION_STATUS.md`：E0～E6 的 source/host/board/measurement 证据矩阵、哪些修改已提交、哪些被外部环境阻塞。
- `docs/SYNC_DESIGN.md`：S1/S2/S3 定义、时间戳来源及误差、匹配策略、伪同步降级限制、时序图和 P95/P99 计算。
- `docs/BSP_CAMERA_ISP.md`：真实 Sensor/板卡/BSP 版本，driver/DTS/rkAIQ 代码路径与 patch 清单、Media graph、曝光控制、IQ 说明、构建和回滚。
- `docs/PIPELINE_AND_OWNERSHIP.md`：真实 thread/queues、FramePair/Result/Packet 结构、RGA/MPP 同步点、DRM/OSD/compose 映射、释放时序。
- `docs/BOARD_ACCEPTANCE.md`：依次测试单路 cam0、单路 cam1、双路、software-sync、hardware-sync（若支持）、RKNN/OSD、RTMP、MP4、异常、稳定性；每个命令的真实结果及日志位置。
- `docs/PERFORMANCE.md`：同步时差分布、AI result age、FPS、CPU/RSS、frame drops、延迟曲线与实验条件。未测填 `NOT_MEASURED`。
- README 命令行包含 Camera0/1 路由、拼接输出尺寸、同步策略、AI 模型/NPU core 配置、RTMP 单一路 URL、录制目录及日志示例，不得把示例 `/dev/videoX` 当真实板端节点。

### 推荐 CLI（按现有 options 风格实现，不要死抄名字）

```bash
./app --camera0 /dev/videoX --camera1 /dev/videoY \
  --input-width 1920 --input-height 1080 --fps 30 \
  --output-width 1920 --output-height 1080 \
  --sync-mode software --sync-tolerance-ms 5 --sync-max-wait-ms 50 \
  --sync-policy drop-older --ai-mode async --model ./behavior.rknn \
  --npu-core0 0 --npu-core1 1 --ai-fps 10 \
  --rtmp-url rtmp://SERVER/live/dual-camera \
  --record --output-dir ./recordings --segment-sec 300 \
  --duration-sec 600 --stats-interval-sec 5
```

`/dev/videoX`、SERVER、输出尺寸及核绑定仅为参数示例。默认保留 `--no-ai`、`--no-stream`、`--no-record` 与有限时运行用于排查。

### 必须新增的测试案例

1. 同步：准确时间戳、跨帧错配、两个速率不同、时间戳乱序、clock_domain 不同、missing frame、队列满、停止释放全部 lease。
2. ISP：Media graph 错误、两 Camera 实际节点映射校验、rkAIQ adapter 权限/版本不匹配、重复初始化检测、不支持 control fail-fast。
3. 画面：左/右顺序、NV12 padding、letterbox/crop 坐标换算、bbox clipping、按 camera_id 配对、过期框不画、strict 模式有限等待。
4. 推流/存储：packet ownership、SPS/PPS、PTS/DTS、RTMP 重连、MP4 分段正确 finalize、网络断流时录像独立、磁盘满时直播独立。
5. 并发：编码慢、AI 慢、某路 Camera 断流、MPP timeout、STREAMOFF 失败、Ctrl+C、反复启动/退出，清理无死锁及 buffer 泄漏。

## 8. 最终报告格式与停止条件

最终在聊天回复：

1. 实际改动文件和 commit/diff，哪些旧模块被复用，为什么不是重建。
2. E0～E6 的证据状态与构建测试命令/退出码。
3. 两路帧时戳 `abs(delta)` P50/P95/P99 与来源；如没有板端数据写 `NOT_MEASURED`。
4. ISP 源码：Sensor driver/DTS/rkAIQ 适配/Media graph 的真实 diff 和当前可编译性；如果不能做真实硬件相关修改说明缺失的**具体资料**，不可写“硬件缺失所以 ISP 软件略过”。
5. 推流/拼接/画框结果的实际播放器、录像检查证据，不能伪造。
6. **独立四项结论**：`SOURCE_IMPLEMENTATION`、`BUILD_TEST_STATUS`、`HARDWARE_SYNC_STATUS`、`END_TO_END_BOARD_STATUS`。

**只有全部当前环境可执行的软件任务都实际完成并通过可运行测试，且所有硬件阻塞项逐条列明后，才结束。** 不允许用 TODO、假 SDK、空函数、只读探测脚本替代核心实现，不允许把软件配帧称为硬件曝光同步。
