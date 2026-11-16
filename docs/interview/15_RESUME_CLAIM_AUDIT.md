# 简历表述真实性审计

本表只允许使用当前仓库和已有审查记录能够支撑的表述。`SOURCE_IMPLEMENTED` 代表源码存在，不代表已构建、已上板或已测得性能。

| 简历表述 | 源码证据 | 构建证据 | 运行证据 | 当前是否可以使用 |
|---|---|---|---|---|
| 设计并实现双路 V4L2 Camera 用户态采集链路 | `main.cpp:19-101`；`dual_camera_pipeline.cpp:387-454`；`v4l2_isp_camera.cpp:203-698` | 无 C++ 构建记录 | 无 RK3588 实机 | **可以，写“源码实现/待板端验证”** |
| 支持 NV12/NV12M plane、stride、offset、bytesused 校验 | `v4l2_isp_camera.cpp:506-698`；`dual_camera_pipeline.cpp:48-75` | 无 C++ 构建记录 | 无真实驱动布局测试 | **可以，限定为源码级契约** |
| 实现 Camera Buffer Lease，最后一个消费者释放后 QBUF | `camera_frame.h:76-103,133-167` | 无 C++ 构建记录 | 仅有测试源码，未执行 | **可以，写“设计/实现”** |
| 实现两路时间戳软件配帧和 drop-older/hold-last 策略 | `frame_pairer.cpp:30-253`；`docs/SYNC_DESIGN.md` | 无 C++ 构建记录 | 算法测试源码存在，执行状态未形成 C++ 证据 | **可以，不能写硬件同步** |
| 实现 Sensor/CSI/RKISP/rkAIQ 适配 | 仓库没有 Sensor driver、DTS、IQ 或 rkAIQ 控制 API；`docs/BSP_CAMERA_ISP.md` 明确缺失 | 无 | 无 | **不可以，删除或改为“了解适配边界”** |
| 完成两颗 Sensor 同时曝光硬件同步 | `sync_mode=hardware` 当前无 BSP backend，fail-fast | 无 | 无 FSIN/Trigger/示波器/SOF 证据 | **不可以** |
| 每路使用独立 RKNN context，结果绑定 sequence/timestamp | `rknnPool.hpp:63-309`；`dual_camera_pipeline.cpp` AI loop | 无 RKNN SDK 构建 | 无模型和 NPU 运行记录 | **可以，写“源码设计/实现”** |
| 完成 NPU core0/core1 绑定优化 | `include/rknnPool.hpp:89-103` 确实调用 `rknn_set_core_mask`，但 `lib/dual_camera_pipeline.cpp:435` 对两个 context 都固定传 `n=0`，`main.cpp:69-70` 解析出的 CLI 核参数未接入；运行时只打印 warning | 无 | 无 | **不可以** |
| 使用 RGA 做 NV12/BGR 转换与缩放、CPU/OpenCV 做保守回退 | `dual_camera_pipeline.cpp:77-100`、compose 逻辑 | 无 RGA/OpenCV 构建 | 无板端图像结果 | **可以，限定为源码实现** |
| 实现检测框与标签 OSD，并按时间戳避免过期结果 | `dual_camera_pipeline.cpp` `compose_pair`；`composite_processor.cpp:108-153` | 无 | 无真实视频/模型输出 | **可以，写“已实现逻辑，待运行验证”** |
| 使用 MPP 硬件编码 H.264 | `mpp_encoder.cpp:42-475`；`dual_camera_pipeline.cpp` `encode_loop` | MPP SDK 缺失，未构建 | 无板端码流 | **谨慎可用，写“接入/实现调用路径”，不可写稳定性能** |
| SPS/PPS header 正确写入 H.264 输出 | `dual_camera_pipeline.cpp` header 写入；`mpp_encoder.cpp:440-475` | 无 | 无 ffprobe/播放器 | **只能写“实现处理逻辑”，不能写可播放已验证** |
| 一次编码、多目的分发到 RTMP 和 MP4 | `encoded_packet_fanout.cpp:5-62`；`dual_camera_pipeline.cpp` `FfmpegPipe`/sink | 无 ffmpeg/MPP 构建 | 无服务器、播放器和分段文件 | **可以写“设计/实现分发框架”，不可写已稳定推流** |
| 完成零拷贝 DMA-BUF pipeline | Camera 只做 EXPBUF 记录；数据实际通过 CPU packed copy，见 `dual_camera_pipeline.cpp:48-75` | 无 | 无 DMA-BUF import/fence/cache 证据 | **不可以，明确当前不是零拷贝** |
| 实现 Capture/AI/Encode 多线程和有界队列 | `dual_camera_pipeline.cpp` capture/AI/encode threads；`bounded_frame_queue.h` | 无 C++ 构建 | 无压力/长稳 | **可以，限定为源码结构** |
| 实现 RK3588 ISP 调优、AE/AWB、曝光/增益控制 | `aiq_controller.cpp` setter 当前返回 `unsupported`；无真实 backend | 无 | 无 | **不可以** |
| 通过 CMake/CTest/ASAN 长稳验证 | `docs/CODEX_EXECUTION_STATUS.md` 明确 CMake/C++ 未执行 | 未通过/未执行 | 无 | **不可以** |
| 达到 X FPS、Y ms 延迟、Z% 丢帧改善 | 无 `MEASURED` 数据；`docs/PERFORMANCE.md` 要求板端补测 | 无 | 无 | **不可以填写数字** |

## 当前已验证版可用句式

> 设计并实现基于 C++17 的双路 V4L2 NV12/NV12M 用户态视频链路：完成 plane/stride/bytesused 校验、CameraFrame lease 生命周期管理、时间戳软件配帧、每路 RKNN 推理结果与帧序号关联、RGA/CPU 图像转换和左右画面 OSD 拼接，并接入 MPP H.264 编码包的 raw/RTMP/分段 MP4 分发框架；当前已完成源码级实现，RK3588 板端、厂商 SDK、Sensor/rkAIQ 和端到端性能仍待验证。

这句话可以进入简历，但应在面试时主动说明：当前环境只完成 Python/结构级检查，C++/MPP/RGA/RKNN 构建和真实板端运行尚未形成证据。

## 目标完成后版（草稿，当前不可使用）

以下内容必须等到相应证据补齐后才可启用：

- “在 RK3588 双路 MIPI Sensor 上完成实采和 Media Controller graph 验证”；需要 `media-ctl -p`、`v4l2-ctl --all`、设备日志和原始帧。
- “完成 rkAIQ AE/AWB/曝光/增益控制”；需要真实 API、IQ 匹配、控制回读和失败码。
- “完成 MPP completion/flush、RTMP/MP4 播放和长稳验证”；需要目标 SDK、ffprobe/播放器、30/60 分钟统计。
- “实现零拷贝或 DMA-BUF import”；需要 import/fence/cache coherency 代码和板端压力证据。
- “性能达到具体 FPS/延迟/丢帧指标”；需要统一测试条件、原始统计和可复现脚本。

在证据补齐前，目标版只能标为“目标草稿”，不能替换当前已验证版。

## 第五轮审查补充

以下表述在当前状态下只能写成“源码实现/待验证”：

| 表述 | 证据 | 当前结论 |
|---|---|---|
| 完成双路 IMX415 Sensor Bring-up | 仓库无 kernel Sensor driver、DTS、I2C/媒体图日志 | 不可使用 |
| 完成 RKISP/rkAIQ/AE/AWB 调试 | `lib/aiq_controller.cpp:102-124` 全部 fail-closed | 不可使用 |
| 完成零拷贝 DMA-BUF pipeline | `EXPBUF` 仅记录 fd，数据仍经 CPU copy | 不可使用 |
| 完成实时 RTMP/MP4 稳定输出 | `FfmpegPipe` 存在，但无 C++/板端/播放器证据，sink 错误未可靠传播 | 不可使用 |
| 实现双路软件配帧 | `lib/frame_pairer.cpp` 和测试源码存在 | 可以，注明未板测且不是硬件同步 |
| 实现 NV12/NV12M 生命周期管理 | `lib/v4l2_isp_camera.cpp`、`include/camera_frame.h` | 可以，注明 C++ 未构建 |
| 实现 RAW10/RAW12 离线分析 | `scripts/isp_quality_analyzer.py`、9 个 Python 用例通过 | 可以，不能写成 ISP 算法实现 |

简历最终检查规则：每个动词后面都能给出源码路径、构建状态、运行状态和验证缺口；如果只能给出 README 或模板，不写“完成”。
