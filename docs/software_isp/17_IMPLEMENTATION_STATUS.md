# Software ISP 当前实现状态

以 [当前架构](../CURRENT_ARCHITECTURE.md) 和 [执行证据](../../Plan/08_TEST_RESULTS.md) 为准，核对日期 2026-11-16。

| 模块 | 当前源码能力 | 限制 |
|---|---|---|
| VirtualSensor/RAW10 | 虚拟场景、Bayer、打包解包、曝光/增益与时间戳 | 非真实 IMX415 采集 |
| SoftwareIsp | RAW10 → RGB/NV12、软件 AWB、颜色/降噪/锐化和统计 | 非 RKISP/rkAIQ，实际成像质量不能据合成图断言 |
| AeController/profile | 软件曝光/增益反馈、软件 profile 读写与应用接口 | 非真实 Sensor 寄存器控制；CLI 不提供实时文件监视 |
| Demo AI | 确定性模拟结果 | ONNX/RKNN 后端未接入，无模型精度证据 |
| camera_isp_demo | 一路/两路串行虚拟处理、配帧统计、首帧导出 | 拼接不由匹配 FramePair 驱动；不输出完整视频 |
| reports/isp | Python 辅助合成样例 | 不能证明 C++ AE/AWB 收敛或实际处理帧率 |

基线已在 MSVC Debug 下编译成功，首轮 CTest 10/11 项通过且有 Software ISP 断言失败。最终修正按用户要求未再构建/测试，详见执行证据；不宣称全部通过或板端通过。
