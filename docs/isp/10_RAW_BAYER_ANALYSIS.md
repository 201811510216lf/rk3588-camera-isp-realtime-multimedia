# RAW Bayer 分析

## Packing

RAW10 CSI 常用 4 个 10-bit 像素打包为 5 bytes；RAW12 常用 2 个 12-bit 像素打包为 3 bytes。实际 CSI unpack/stride 仍以节点格式和 sensor mode table 为准，工具对尾部不完整 group 报错。

## 工具与路径

`scripts/isp_quality_analyzer.py bayer` 读取 packed file，按 pattern 分离 R/Gr/Gb/B，应用 black level 后输出 JSON/文本统计。源码路径：`scripts/isp_quality_analyzer.py`。

## 验证

使用已知递增像素向量做 round-trip 测试；板端通过 `v4l2-ctl --list-formats-ext` 和真实 RAW capture 证明 packing，再分析噪声/边缘。没有 RAW 时只能分析 NV12 Y 平面，并列出限制。

## 状态

SOURCE_IMPLEMENTED：主机解析器；HOST_TESTED：以 Python 单元测试为准；BOARD_VERIFIED/MEASURED：NOT_TESTED。

## 面试要点

先确认 bit depth、packing、stride、Bayer order 和 black level，任何一个不明都不能给出可信的通道统计。
