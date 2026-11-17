# RK3588 双路 Camera 大鼠行为监测

面向实验室连续监控：两路真实 Camera 经 RK3588 硬件 ISP 输出 NV12/NV12M，执行异步大鼠检测/跟踪/时间行为分析，并保存视频与结构化事件。当前为软件实现与接入阶段，没有开发板、SDK、真实行为模型或24小时板测证据。

| 验证层 | 当前状态 |
|---|---|
| C++17 软件核心与自动测试 | Windows/MSVC与Linux/GCC（含ASan/UBSan），各18/18通过 |
| 模型/事件/录像索引软件 | 已实现，使用内存 fixture；行为模型未提供 |
| Linux V4L2、RGA、MPP、RKNN 目标程序 | 源码接线完成，SDK 编译/板端验证待完成 |
| 双真实 Camera、ISP/3A、模型精度、24/72h长稳 | BLOCKED_HARDWARE |
| MPP EOS/显式PTS/拆分输入输出线程、VFR容器时间轴 | PARTIAL，见适配清单 |

## 主链路

Sensor0/1 → MIPI/RKCIF/RKISP → 两路 V4L2 NV12/NV12M → 共享 Frame lease → 独立录像队列 / 软件配帧合成 → RGA/CPU标明的图像处理、精确帧OSD → MPP H.264 → 各自有界 raw/MP4/network sink。AI 使用独立有界队列，结果保留原 Camera/sequence/timestamp/model 信息。

RkAiq 是外部 ISP 3A/IQ 控制，不是串行图像硬件节点。主产品只有两路真实 Camera，无文件、USB、RTSP 或 Mock 输入模式。`--video-mode independent` 两路录像互不等待配帧；`joint` 是配帧合成录像，不能视为两路原始完整录像。软件配帧不等于同时曝光。

## 主机构建

```bash
bash scripts/run_tests.sh
bash scripts/run_stress_tests.sh
```

Windows 已安装 Visual Studio C++ 与 Windows SDK 时：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_host_windows.ps1
```

CMake >=3.16，C++17、Threads::Threads、GCC/Clang警告开关和 compile_commands.json；Windows 脚本 clean-first 避免本机旧依赖记录残留。主机测试不编译厂商视频实现。Linux CI 已通过普通与ASan/UBSan构建及各400次压力测试；真实执行结果见 [测试记录](Plan/08_TEST_RESULTS.md)。

## BSP 构建和运行

在瑞芯微官方 RK3588 BSP 配套 Linux 工具链中提供真实 SDK：

```bash
cmake -S . -B build-rk -DBUILD_RK_DEMO=ON -DREQUIRE_RK_DEMO=ON \
  -DENABLE_V4L2=ON -DENABLE_ROCKCHIP_RGA=ON -DENABLE_ROCKCHIP_MPP=ON -DENABLE_RKNN=ON \
  -DRGA_PATH=/opt/bsp/rga -DMPP_PATH=/opt/bsp/mpp -DRKNN_API_PATH=/opt/bsp/rknn \
  -DOpenCV_DIR=/opt/bsp/opencv/lib/cmake/opencv4
cmake --build build-rk --parallel
./build-rk/myDemo --camera0 /dev/videoX --camera1 /dev/videoY \
  --pixel-format auto --capture-buffers 6 --video-mode independent \
  --input-width 1920 --input-height 1080 --output-width 1920 --output-height 1080 --fps 30 \
  --no-ai --record --output-dir /data/recordings --output /data/recordings/archive.h264 \
  --segment-sec 300 --min-free-mb 1024 --no-stream --duration-sec 600
```

节点必须由实际 media graph 确认，不默认使用 video0/1。上述命令是板端接入示例，尚未实际运行。SDK 开关开启而依赖缺失会明确失败；应用侧 ENABLE_RKAIQ 尚未适配，直接报错，应使用 BSP aiq 服务。

AI 需增加 `--ai-mode async --model /data/rat.rknn --model-contract /data/rat.contract --events /data/recordings/events.jsonl`。模型契约必须来自导出 metadata，详见 [RKNN 接入](docs/RKNN_BACKEND_ADAPTATION.md)。当前 adapter 仅支持匹配的 YOLOv5 三头 INT8 schema；普通 rat detector 的 behavior_classes 留空，不虚构行为。`--ai-mode strict` 因未实现而报错。行为状态 OSD 目前为 UNAVAILABLE，事件另存 JSONL。

网络采用 `--publish-url rtsp://SERVER/app/stream` 或 `rtmp[s]://...`，保留旧 --rtmp-url。每个 sink 独立队列/线程；网络故障不阻塞录像。录像队列满、磁盘阈值或写入失败会显式停止并报错，原始数据保留。磁盘写入依赖操作系统，未声称硬实时保证。

MP4 关键帧分段、先写 partial，成功 finalize 后改名并记录索引。FFmpeg 由 H.264 重建 CFR 时间轴，源采集间隔仅存于索引，尚不支持保留采集间隔的 VFR mux。索引为 joint-index.jsonl / cam0-index.jsonl / cam1-index.jsonl。事件关联：

```bash
python tools/associate_events.py --events /data/recordings/events.jsonl \
  --index /data/recordings/cam0-index.jsonl --recording-dir /data/recordings \
  --output /data/recordings/associated-cam0.jsonl
```

每路分别选取对应事件和索引；缺失视频/未 finalize 标为 partial/unmapped。默认不自动删除实验录像。

## 保留的独立实验

`camera_isp_demo`、`src/camera/`、`src/isp/` 保留 VirtualSensor RAW10/Software ISP 实验。它是独立工具，不连接 RKISP NV12 主链路、不替代 RGA/MPP/RKNN，Demo AI 明确为模拟。旧合成 reports 已清理；实验算法及文档仍保留。

## 工程与交付

- include/ 公共接口，src/ 实现，tools/ 工具，tests/ 主机测试，configs/ ISP配置，lib/ 仅本地SDK说明。
- [完整架构](Plan/FINAL_ARCHITECTURE.md)、[实现状态](Plan/PROJECT_STATUS.md)、[硬件适配](Plan/HARDWARE_ADAPTATION_ROADMAP.md)、[最终交付](Plan/FINAL_DELIVERY_REPORT.md)。
- [清理清单](Plan/CLEANUP_MANIFEST.txt)记录被替代计划、旧审查/面试材料、生成报告、废弃单路入口和SDK重复头；保留成果可从 Git 历史追溯。本轮未复制参考工程源码/二进制。
