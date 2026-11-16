# RK3588 双路 Camera 与 Software ISP

当前源码包含两个独立入口。项目已从早期单路 MIPI 方案发展到双路 Camera；旧计划和审查报告保留为阶段记录。

| 入口 | 当前实现 | 运行条件与边界 |
|---|---|---|
| `myDemo`：`main.cpp` → `DualCameraPipeline` | 两路 V4L2 NV12/NV12M → 软件时间戳配帧；两路可选 RKNN → OSD/左右拼接 → 一个 MPP H.264 编码器 → raw H.264、RTMP/分段 MP4 | Linux/RK3588、真实节点、RKNN/MPP/RGA/OpenCV SDK；尚未目标构建或板端验收 |
| `camera_isp_demo`：`tools/camera_isp_demo.cpp` | VirtualSensor RAW10 → Software ISP → RGB/NV12、软件 AE/AWB、可选 Demo AI、双路元数据配帧和首帧拼接样例 | 独立 C++17 主机模块；模拟输入，不连接真实 rkAIQ/RKISP/RKNN/MPP |

阅读入口：[当前架构与源码索引](docs/CURRENT_ARCHITECTURE.md)、[当前证据与验证状态](docs/CODEX_EXECUTION_STATUS.md)、[本轮代码差异审查](docs/CODE_RECONCILIATION_2026-11-16.md)。`lib/mipi_pipeline.cpp` 是保留的旧单路实现，不在当前 `myDemo` 构建源清单中。历史文档中的“当前”、行号和测试结果只适用于其记录阶段。

## 双路目标程序

下面是目标板配置示例，节点、模型和服务器必须先由实际 BSP/设备确认。图像到达 V4L2 之前的 Sensor/CSI/RKISP/rkAIQ 属于外部板级前置条件；本程序不负责初始化 rkAIQ。

```bash
./build/myDemo --camera0 /dev/videoX --camera1 /dev/videoY \
  --input-width 1920 --input-height 1080 --fps 30 \
  --output-width 1920 --output-height 1080 \
  --sync-mode software --sync-tolerance-ms 5 --sync-max-wait-ms 50 \
  --sync-policy drop-older --ai-mode async --model /data/models/behavior.rknn \
  --rtmp-url rtmp://SERVER/live/dual-camera \
  --record --output-dir /data/recordings --segment-sec 300 \
  --duration-sec 600 --output /data/dual.h264
```

AI、推流、分段录像分别可用 `--no-ai`、`--no-stream`、`--no-record` 关闭；这些参数不会把双路入口变为单 Camera 模式。默认输出为 1920×1080 左右 letterbox。`FramePairer` 只做同一时钟域的时间戳配帧；`--sync-mode hardware` 因没有硬件触发后端而直接失败，不能据配帧结果声称同时曝光。

RTMP/MP4 通过外部 `ffmpeg` 子进程消费同一 H.264 包流。分发器同步调用各 sink，慢网络/磁盘会回压编码线程；当前没有每 sink 的独立队列或有界子进程关闭保证。DMA-BUF import、硬件 completion、RKNN 推理、MPP/RGA、断线恢复和长稳均需目标证据。

## 独立 Software ISP 实验

```bash
./build/camera_isp_demo --sensor virtual --width 640 --height 360 --fps 30 \
  --duration 3 --format raw10 --enable-ae --output=output/demo --report=output/reports-single
./build/camera_isp_demo --camera-count 2 --sensor virtual --width 640 --height 360 \
  --fps 30 --duration 3 --format raw10 --enable-ai --enable-composite \
  --output=output/dual --report=output/reports-dual
```

以上为单配置生成器的路径；Visual Studio Debug 构建的程序位于 `build-review-msvc/Debug/`。当前 demo 导出首帧 PPM、可选首帧拼接 NV12 和 JSON 统计，并不导出整段 NV12 视频。双路循环串行处理两台 VirtualSensor，配帧统计与首帧拼接输出仍是分开的演示逻辑。`measured_fps` 是无实时节流的主机吞吐，不能代表实际 Camera FPS。

`reports/isp/` 中由 `scripts/run_round6_reports.py` 生成的曲线和 JSON 是辅助合成数据，不能证明 C++ AE/AWB 收敛或成像质量。Demo AI 是模拟检测；Software ISP 的 YAML profile 不等于 Rockchip IQ。详见 [Software ISP 状态](docs/software_isp/17_IMPLEMENTATION_STATUS.md)。

## 构建与检查说明

主机不需要 RK3588 SDK，示例命令供后续复现：

```bash
cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON -DBUILD_BSP_TOOLS=ON
cmake --build build-host --config Debug --parallel
ctest --test-dir build-host -C Debug --output-on-failure
python -m unittest discover -s tests -p '*isp_quality_analyzer*.py'
```

目标板/交叉构建需提供 RKNN、MPP、RGA、OpenCV SDK，并使用 `-DBUILD_RK_DEMO=ON -DREQUIRE_RK_DEMO=ON` 避免依赖缺失时跳过目标。主机编译不包含 `myDemo`，不能证明 Linux/厂商接口可构建。

本轮已找到 Visual Studio 自带的 CMake/MSVC，旧文档中的“PATH 无工具”不等于机器未安装工具。已执行检查和未通过项逐项记录在[状态文档](docs/CODEX_EXECUTION_STATUS.md)；依用户后续要求，修正后的版本仅审查差异，不再运行构建或测试。

板端准备与验收见 [BSP/ISP](docs/BSP_CAMERA_ISP.md)、[同步设计](docs/SYNC_DESIGN.md)、[板端验收](docs/BOARD_ACCEPTANCE.md)。
