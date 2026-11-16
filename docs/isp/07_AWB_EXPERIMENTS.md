# AWB 实验设计

## 目标

用 Bayer channel 统计、R/G 与 B/G 比例和灰卡/不同光源样本评估 AWB 收敛及色偏。

## 当前状态

`isp_quality_analyzer.py bayer` 支持 RGGB/GRBG/GBRG/BGGR 统计；真实 AWB 控制和 3A stats 需要 rkAIQ backend，当前返回 Unsupported。没有实拍 RAW，不能给出色温或收敛数值。

## 实验步骤

对白平衡目标、日光、暖光各采样固定帧数，保存 sensor mode、bit depth、black level、曝光/增益回读和统计 CSV；比较收敛前后 R/G、B/G 及帧间方差。

## 命令与状态

```bash
python3 scripts/isp_quality_analyzer.py bayer --input raw10.bin --width 3840 --height 2160 --packing raw10 --pattern rggb --black-level 64
isp_debug_tool --awb-status --camera 0
```

BOARD_VERIFIED：NOT_TESTED；NOT_MEASURED：色偏、收敛时间、光源对比。

## 面试要点

软件统计可检查色偏，但不能把离线通道比例称作 ISP AWB 已闭环。
