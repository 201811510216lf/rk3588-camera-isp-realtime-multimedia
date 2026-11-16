# 三段经历的差异化定位

## 资料限制

仓库只包含当前双路 RK3588 项目。显微镜实习和大鼠体温监测项目仅依据用户提供的经历摘要比较，没有源码、构建日志或测试报告，不能把其中的数字、个人贡献和板端结果写成已审查事实。

## 推荐排序

| 目标岗位 | 推荐顺序 | 原因 |
|---|---|---|
| Camera / ISP | 1. 双路 Camera；2. 显微镜传输；3. I2C 体温 | 双路项目最接近 V4L2、ISP 输出、时间戳和 Camera 生命周期；显微镜作为 DMA-BUF/编码补充。 |
| Linux Multimedia | 1. 显微镜传输；2. 双路 Camera；3. I2C 体温 | 显微镜摘要直接覆盖 MPP、RGA、H.264、RTSP/WebRTC 和低延迟传输；双路项目补充采集与 AI。 |
| Linux Driver / BSP | 1. I2C 体温；2. 双路 Camera；3. 显微镜传输 | I2C、字符设备、kthread、waitqueue、spinlock、epoll、ioctl、Buildroot 与驱动岗位更直接。 |
| 嵌入式 C++ | 1. 双路 Camera；2. 显微镜传输；3. I2C 体温 | 双路项目展示 C++ 并发、RAII、队列、资源所有权；其余项目作为系统接口和性能背景。 |

## 技术重叠与差异

| 项目 | 可强调 | 与其他项目的差异 |
|---|---|---|
| 显微镜低延迟传输实习 | V4L2、DMA-BUF、MPP、RGA、H.264、RTSP/WebRTC | 更偏单路采集和传输协议；缺少当前仓库证据，面试时按实习职责回答。 |
| 双路 RK3588 Camera/ISP | 两路 V4L2、时间戳配帧、Camera lease、RGA/OSD、RKNN、单 MPP fan-out | 更偏 Camera pipeline 和跨线程资源生命周期；ISP/Sensor 硬件适配尚未由仓库证明。 |
| I2C 驱动与体温监测 | I2C、字符设备、kthread、waitqueue、spinlock、epoll、ioctl、Buildroot | 更偏内核/驱动和事件采集，不应与 Camera ISP 经验混写。 |

## “你做的都是 SDK 集成吗？”回答框架

可以承认 SDK 依赖，同时说明自己负责的工程边界：我没有把厂商 BSP 或 ISP 驱动冒充成自研；在用户态实现了 V4L2 buffer/layout 校验、FramePairer、lease 所有权、有界队列、RGA/CPU 组合、RKNN 结果时序、MPP packet fan-out 和错误回收。对 Sensor/DTS/rkAIQ 仍明确列为待板端补证，这样能区分 SDK 调用和系统设计责任。

## 可能质疑与回答

- **“双路相机是否硬件同步？”** 当前只有软件时间戳配帧，硬件模式无 verified backend，不能宣称同时曝光。
- **“你改过 ISP 驱动吗？”** 当前仓库没有 Sensor/DTS/内核源码；能展示的是用户态 V4L2 接入和只读 media graph/rkAIQ 状态工具。
- **“显微镜和双路项目重复吗？”** 都用到 V4L2/MPP/RGA，但前者重点是低延迟传输和协议，后者重点是双路配帧、AI 时序和共享编码包。
- **“体温项目与 Camera 岗位有什么用？”** 它证明我理解 ioctl、阻塞唤醒、并发访问和 Buildroot，能迁移到 Camera driver 的设备控制和事件处理。

## 简历写法建议

当前双路项目放在第一位时，使用 `10/11/12_RESUME_*.md` 的“当前已验证版”措辞；其他两段只能写用户自述已做内容，并在面试前补充源码、提交记录、测试日志和个人贡献边界。


## 第五轮校准（2026-11-13）

本仓库当前没有显微镜实习或 I2C 体温项目的源码、提交记录、构建日志或测试报告；对这两段经历的判断只使用用户在任务中提供的摘要。当前 RK3588 项目可由源码证明的范围是用户态 V4L2/NV12/NV12M、软件配帧、Buffer lease、RKNN/RGA/OpenCV、MPP 和多 sink 结构。IMX415 Sensor driver、DTS、RKISP、rkAIQ、IQ、AE/AWB、同步曝光和板端 RTMP/MP4 仍无证据。

简历推荐顺序不变，但表述要按岗位切换：

- Camera/ISP：双路项目第一，突出 V4L2 layout、FramePairer、bring-up 证据边界；把 Sensor/rkAIQ 写成“适配准备/待上板”。
- Linux Multimedia：显微镜实习第一（仅基于个人摘要），突出低延迟传输协议；双路项目第二，突出 packet fan-out、MPP/RGA 和资源生命周期。
- Linux Driver/BSP：I2C 体温项目第一（仅基于个人摘要），双路项目作为 V4L2/BSP 迁移场景。
- Embedded C++：双路项目第一，突出有界队列、shared ownership、线程停止协议和错误分层。

面试中不要把三个项目共用的“V4L2/MPP/RGA”技术名词当成同一份证据；应分别讲清采集对象、个人职责、构建/运行记录和未验证部分。
