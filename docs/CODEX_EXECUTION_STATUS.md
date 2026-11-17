# 当前执行状态

本轮基于main=1119530，已完整阅读新任务文件，重点改进真实V4L2 RKISP输出采集与原Pipeline接线。保留原DualCameraPipeline、FramePairer、RKNN/RGA/MPP/FFmpeg及独立Software ISP；没有新增RkAiq Backend抽象或产品输入。

| 验证层 | 当前结果 |
|---|---|
| Windows/MSVC clean build | 26/26通过，5.01秒 |
| Windows/MSVC ASan clean build | 26/26通过，25.86秒，无ASan报告 |
| Linux正式V4L2类、syscall测试、isp_capture CLI | 等待本轮CI，不能用旧CI代替 |
| RkAiq真实SDK / 厂商myDemo编译 | BLOCKED_HARDWARE，未配置匹配头/库/核验port |
| 真实Camera、IQ/3A、RKNN/MPP输出与长稳 | BLOCKED_HARDWARE，无板端测量 |

本轮改动与最终Linux/上传证据见[实现](../RK3588_ISP_IMPLEMENTATION.md)和[测试报告](../RK3588_ISP_TEST_REPORT.md)。单路抓帧与双路顺序见[采集](../RK3588_ISP_CAPTURE.md)、[双路](../RK3588_DUAL_CAMERA.md)。当前主链路有MMAP到CPU打包复制，EXPBUF不等于下游DMA-BUF导入。

前轮a436b5f控制层与其CI历史保留在[增量交付](../Plan/RKISP/INCREMENTAL_CONTROL_REPORT.md)，没有替代本轮验证。
