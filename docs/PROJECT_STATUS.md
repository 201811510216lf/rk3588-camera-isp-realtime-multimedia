# 当前项目状态

当前任务从main=1119530继续完善RKISP直接输出NV12/NV12M的采集。主业务仍是双路真实Camera；Software ISP实验保留，未接入产品输入。

| 功能 | 源码状态 | SDK/板端边界 |
|---|---|---|
| V4L2IspCamera | 真实Linux ioctl、G_FMT、帧间隔、plane/payload、MMAP/lease清理已完善 | Linux构建/测试见本轮报告；真实驱动待测 |
| Camera 0/1配置 | 文件逐路驱动node/尺寸/FPS/格式，显式CLI冲突拒绝 | 节点/Sensor/共享ISP资源必须来自实际拓扑 |
| 原Pipeline接入 | 复用FramePairer/AI/RGA/MPP/FFmpeg，按实际帧布局处理 | 厂商myDemo未在此机SDK编译 |
| 单路isp_capture | 正式Camera类的有界NV12抓帧/元数据工具 | 真实图像尚未获取 |
| ISP检查/准备 | 沿用现有AiqController，协商前准备、协商后重读实际输出 | 外部服务发现不等于IQ/AE/AWB确认 |
| RkAiq真实API | 最小CMake依赖检测保留 | SDK_BACKEND_MISSING / BLOCKED_HARDWARE |
| DMA-BUF与缓存 | MMAP+可选EXPBUF，当前CPU打包 | 未实现外部分配/DMABUF导入，不声称零拷贝 |
| SDK/模型/板端/24h | 待提供环境与验收 | BLOCKED_HARDWARE |

本轮Windows普通与ASan各26/26通过；Linux普通与ASan+UBSan各28/28通过，Windows及两个Linux job各800次压力通过。Linux证据及交付文件见[测试报告](../RK3588_ISP_TEST_REPORT.md)。整体视频/AI功能历史保留在[Plan项目状态](../Plan/PROJECT_STATUS.md)，不得把主机fixture等同于真实图像/行为/长稳结论。
