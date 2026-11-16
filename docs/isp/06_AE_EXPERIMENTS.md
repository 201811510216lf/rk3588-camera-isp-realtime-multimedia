# AE 实验设计

## 目标

记录曝光时间、模拟/数字增益、Y 亮度、过曝/欠曝比例及收敛时间，区分控制请求、驱动回读和图像实测。

## 当前状态

`isp_debug_tool --ae-status` 在无真实后端时返回 `Unsupported`；`scripts/isp_quality_analyzer.py` 可从 NV12 或 RAW 样本计算亮度/直方图，但不会推断 exposure/gain。`scripts/aiq_sample_capture.sh` 可周期采样状态并标记 NOT_MEASURED。

## 实验步骤

固定光源和目标，先自动曝光稳定，再阶跃改变亮度；每帧记录 monotonic timestamp、亮度统计、控制回读和帧序号。至少重复三次，报告 p50/p95 和饱和比例。

## 命令与结果

```bash
isp_debug_tool --ae-status --camera 0
python3 scripts/isp_quality_analyzer.py nv12 --input frame.nv12 --width 1920 --height 1080
```

BOARD_VERIFIED：NOT_TESTED；当前无实测帧/控制回读。

## 面试要点

AE 收敛不能只看亮度均值，要同时看高光裁剪、暗部噪声和真实 sensor feedback。
