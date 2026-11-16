# 图像质量调试

## 指标

Black level、Bayer 四通道均值/方差、R/G/B 比例、亮度直方图、过曝/欠曝、噪声估计、Tenengrad 边缘锐度、帧间亮度稳定性和 AE/AWB 收敛趋势。

## 工具

`scripts/isp_quality_analyzer.py` 真实解包 RAW10（4 pixels/5 bytes）和 RAW12（2 pixels/3 bytes），支持 Bayer pattern/black level；NV12 路径只分析 Y 平面并明确限制。它不会实现硬件 ISP，也不会伪造曝光或 IQ 参数。

## 状态

SOURCE_IMPLEMENTED：主机离线分析器（待 Python 测试）；BOARD_VERIFIED/MEASURED：NOT_TESTED；HDR/3DNR/镜头阴影效果需要成对 RAW/NV12 实拍数据和 BSP 控制。

## 面试要点

先定义可复现指标和输入 packing，再谈画质；RAW10 不能按 8-bit 灰度直接读取。
