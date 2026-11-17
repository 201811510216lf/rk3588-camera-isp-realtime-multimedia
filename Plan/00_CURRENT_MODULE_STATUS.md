# 初始模块状态
| 模块 | 状态 | 证据/缺口 |
|---|---|---|
| CameraFrame/V4L2 | IMPLEMENTED + HARDWARE_UNVERIFIED | lease/MMAP/多平面，缺独立配置与板测 |
| FramePairer | IMPLEMENTED | 主机测试，统计存储需有界 |
| BoundedFrameQueue | PARTIAL | 缺多策略/超时/指标 |
| RKNN/RGA/MPP | HARDWARE_UNVERIFIED | 既有厂商代码，缺匹配 SDK 构建 |
| Software ISP | MOCK_OR_DEMO（独立实验） | 真实 CPU 算法，VirtualSensor，不能替代 RKISP |
| MP4/RTMP | PARTIAL | FFmpeg 管道同步阻塞 |
| RTSP | MISSING | 需服务端及发布验证 |
| Behavior/Event | MISSING | 不可将 YOLO 类别直接当连续行为 |
| 24h manager | MISSING | 缺健康与存储状态 |
