# 当前架构与源码依据

核对日期：2026-11-16（Asia/Shanghai）。以 `CMakeLists.txt` 的目标源清单和实际入口为准；验证结果见 [CODEX_EXECUTION_STATUS.md](CODEX_EXECUTION_STATUS.md)。

## 1. 两个独立目标

| 目标 | 入口与关键源码 | 职责 |
|---|---|---|
| `myDemo` | [`main.cpp`](../main.cpp)、[`dual_camera_pipeline.cpp`](../lib/dual_camera_pipeline.cpp) | Linux/RK3588 双路 Camera 主链路 |
| `camera_isp_demo` | [`tools/camera_isp_demo.cpp`](../tools/camera_isp_demo.cpp)、`lib/camera/`、`lib/isp/`、`lib/ai/` | 无厂商 SDK 的 VirtualSensor/Software ISP 实验 |
| 主机单元测试 | `tests/`，受 `BUILD_HOST_TESTS` 控制 | 软件契约和辅助工具校验，不覆盖真实板端 |
| BSP 辅助工具 | `media_graph_validator`、`isp_control_cli`、`isp_debug_tool` | 图拓扑检查/状态边界；rkAIQ adapter 未接真实厂商 API |

`include/mipi_pipeline.h`、`lib/mipi_pipeline.cpp` 仍保留，但不在当前 `myDemo` 源清单中。`--source=mipi` 表示输入类型，不表示只有一路 Camera；旧 `--device/--width/--height` 兼容参数不改变双路入口。

## 2. 双路 Camera 主链路

```text
外部板级前置条件：Sensor0/1 → CSI/RKCIF/RKISP → 可采集的两个 V4L2 节点
                                        ↓
V4L2IspCamera[0/1] → FramePairer → bounded pair queue → Compose/OSD → 一个 MPP encoder
        └→ 各自 bounded AI queue → 各自 RKNN context → result snapshot ┘
一个 H.264 包流 → EncodedPacketDistributor → raw 文件 / RTMP ffmpeg pipe / 分段 MP4 pipe
```

- `V4L2IspCamera` 校验 NV12/NV12M、plane/stride/offset/bytesused，CameraState/lease 管理槽位回收。
- `FramePairer` 依据时间戳、时钟域、容差和等待窗口配帧；不控制 Sensor 曝光。硬件同步模式直接失败。
- AI 线程复制输入后释放自己的 camera lease，但配帧队列仍可持有同一帧。结果按 camera/sequence/timestamp 关联。
- 图像转换尝试 RGA，拼接、框映射和保守 fallback 使用 CPU/OpenCV；输出是独立 packed NV12，未实现全链路零拷贝。
- 分发器对 sink 同步顺序调用；共享包数据不代表 I/O 隔离。慢 sink 会阻塞 Encode，并引发上游有界队列丢帧。无“断网仍保证录像”或“有界关闭”验收结论。
- 真实 rkAIQ 初始化、IQ 加载和硬件 AE/AWB 不在此入口内；`AiqController` 仍拒绝未实现的操作。

更多线程/所有权边界见 [PIPELINE_AND_OWNERSHIP.md](PIPELINE_AND_OWNERSHIP.md)。

## 3. Software ISP 实验模块

`VirtualSensor` 生成带虚拟曝光、增益、场景和时间戳的 RAW10；`SoftwareIsp` 完成 RAW10 解包、BLC/DPC/LSC、去马赛克、软件 AWB、CCM/gamma、降噪、锐化及 RGB/NV12 输出。`AeController` 将统计反馈为虚拟传感器曝光/增益，profile 为独立软件配置。

该模块不调用板端 V4L2/RKISP/rkAIQ，也不替代生产入口的 RKNN/RGA/MPP。`ai::BackendKind::Demo` 为合成检测；ONNX/RKNN 分支是未接入的接口占位，不能据其枚举值声称完成真实后端。

`camera_isp_demo` 可配置一台或两台 VirtualSensor；两路在同一循环中串行处理。FramePairer 用于元数据配帧统计；当前首帧 NV12 拼接使用循环中的两张图像，尚未绑定实际返回的 `FramePair`。输出不包含完整逐帧视频、真实模型检测或生产链路的 OSD/推流。

`--fps` 决定虚拟帧数和时间戳步长，当前 `realtime_pacing=false`。报告吞吐是主机处理速率。`scripts/run_round6_reports.py` 使用独立的合成图像和预设曲线；其报告不能当作 C++ 管线运行结果。

## 4. 文档适用范围

[README](../README.md)、本页、[执行状态](CODEX_EXECUTION_STATUS.md)、[同步](SYNC_DESIGN.md) 和 [板端验收](BOARD_ACCEPTANCE.md) 为当前入口。早期单路计划、迁移审计、Round 6 报告和多轮 code review 是历史记录；其中的约束、缺陷行号、环境结论和测试数量不自动延续到最新提交。

历史单路计划被双路计划取代；双路计划仍是需求清单，不能作为完成证明。Software ISP 独立成模块，不将软件 AE/AWB 的实现推广为真实 rkAIQ 控制已完成。
