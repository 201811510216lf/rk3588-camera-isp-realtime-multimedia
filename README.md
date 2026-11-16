# RK3588 双路 MIPI RAW / ISP / 同步配帧 / AI 拼接

当前主链路是：两路真实 Sensor → CSI/RKCIF/RKISP/rkAIQ → 两个独立 V4L2 NV12/NV12M 节点 → `FramePairer` 时间戳匹配 → 每路独立 RKNN context → RGA/CPU 保守转换与 OSD → 左右拼接 → **一个** MPP H.264 Encoder → 同一编码包分发到 RTMP 和分段 MP4。JSONL 只记录事件与诊断。

`FramePairer` 只声明软件配帧和内核时间戳近时刻匹配。它不会把同名 FPS、线程启动顺序、NPU 核绑定或 VPU 并行解释成 Sensor 同时曝光。`--sync-mode hardware` 在没有真实 FSIN/Trigger BSP 后端和板端证据时 fail-fast。RTMP/MP4 sink 通过板端可用的 `ffmpeg` 进程消费同一份 H.264 包；没有 ffmpeg 或服务器时会明确失败/记录未验证。

示例命令（节点、服务器、模型路径必须先由板端探测确认）：

```bash
./build/myDemo --camera0 /dev/videoX --camera1 /dev/videoY \
  --input-width 1920 --input-height 1080 --fps 30 \
  --output-width 1920 --output-height 1080 \
  --sync-mode software --sync-tolerance-ms 5 --sync-max-wait-ms 50 \
  --sync-policy drop-older --ai-mode async \
  --model /data/models/behavior.rknn --npu-core0 0 --npu-core1 1 \
  --rtmp-url rtmp://SERVER/live/dual-camera \
  --record --output-dir /data/recordings --segment-sec 300 \
  --duration-sec 600 --output /data/dual.h264
```

关闭 AI、推流或录像进行分步验收：`--no-ai`、`--no-stream`、`--no-record`。默认输出尺寸是 1920×1080 左右布局；输入比例由 `CompositeProcessor` 做等比例 letterbox，AI 框使用同一映射矩阵。未确认 SDK 的 DMA-BUF import、cache/fence 和异步 MPP completion 前，路径使用 mmap + CPU copy，并将完成语义保持为保守模式。

## 构建与测试

```bash
cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON -DBUILD_BSP_TOOLS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

板端或交叉 Linux 构建要求真实 RKNN、MPP、RGA、OpenCV 兄弟目录；使用 `-DREQUIRE_RK_DEMO=ON` 让依赖缺失直接失败。当前 Codex 环境没有 CMake/C++ 编译器、RK3588 设备、BSP checkout、厂商 SDK 或模型，因此本次的 `BOARD_TESTED`、`TARGET_BUILT`、`MEASURED` 证据均为 `NOT_MEASURED`。

只读探测和 ISP 实验：

```bash
scripts/isp_probe.sh | tee docs/isp_migration/hardware/isp_probe.log
./build/media_graph_validator --media-device /dev/media0 --camera0 /dev/videoX --camera1 /dev/videoY --width 1920 --height 1080 --fps 30
scripts/isp_capture_experiment.sh --device /dev/videoX --frames 120 --out-dir /data/cam0_capture
python3 scripts/analyze_isp_samples.py --nv12 /data/cam0_capture/<timestamp>_videoX.nv12 --width 1920 --height 1080 > /data/cam0_isp.txt
python3 scripts/isp_quality_analyzer.py bayer --input /data/cam0.raw10 --width 3840 --height 2160 --packing raw10 --pattern rggb --json
isp_debug_tool --list
isp_debug_tool --status --camera 0
```

IMX415/RK3588 适配证据、DTS 占位模板、RAW10/RAW12 质量分析、IQ 清单和真实性状态见 `docs/isp/01_IMX415_HARDWARE_COMPATIBILITY.md` 至 `docs/isp/13_ISP_IMPLEMENTATION_STATUS.md`。这些文档明确区分源码、主机测试和板端验证；未知板级值不会被伪造。

## Round 6 Software-first ISP

本轮增加了可脱离 RK3588 SDK 运行的 `VirtualSensor`、标准 RAW10 Pack/Unpack、Software ISP、AE/AWB、IQ profile、双路软件配帧和 Demo AI backend。入口是：

```bash
./build/camera_isp_demo --sensor virtual --width 640 --height 360 --fps 30 --duration 3 --format raw10 --enable-ae --output=output/demo
./build/camera_isp_demo --camera-count 2 --sensor virtual --width 640 --height 360 --fps 30 --duration 3 --format raw10 --enable-ai --enable-composite --output=output/dual
```

`camera_isp_demo` 产生 PPM、NV12 和 JSON 质量/性能报告；`scripts/encode_nv12_with_ffmpeg.py` 可在存在 FFmpeg 时将 NV12 转成 H.264/MP4，并把 RTMP 连接结果原样记录。Demo 检测结果标记为 `SIMULATED`，Software ISP 不冒充 RKISP/rkAIQ，CPU 合成不冒充 RGA，主机 FFmpeg 不冒充 MPP。

新增源码、测试和面试文档见 `docs/software_isp/`，完整状态见 `docs/software_isp/FINAL_IMPLEMENTATION_REPORT.md`。
