# Camera / ISP 知识体系

这份文档把“项目已经写了什么”和“岗位需要掌握什么”分开。当前仓库没有 RK3588 BSP、IMX415 模组、DTS、IQ 或 rkAIQ SDK，因此 Sensor Bring-up、RKISP 出图和 AE/AWB 控制都属于待板端验证内容。

## 1. Linux Camera 总体架构

概念：Linux Camera 通常由 Sensor subdev、CSI-2/DPHY、RKCIF/RKISP subdev、Media Controller graph 和 V4L2 video node 组成。Sensor 通过 I2C 配置寄存器，subdev pad 描述 media-bus format；最终应用从 video node 读出 ISP 处理后的 NV12/NV12M。

为什么项目需要：应用需要稳定地消费两路图像，但不应把 V4L2 输出节点误说成自己实现了 Sensor 或 ISP。

源码实现：`lib/v4l2_isp_camera.cpp:203-482` 执行 `QUERYCAP`、格式枚举、`S_FMT`、`G/S_PARM`、`REQBUFS`、`QUERYBUF`、`MMAP`、`EXPBUF`；`:484-697` 执行 `QBUF/STREAMON/poll/DQBUF/STREAMOFF`。`lib/media_graph_validator.cpp:106-153` 只读调用 media/v4l2 工具并做文本检查。

常见追问：V4L2 能拿到 NV12，是否说明自己做了 ISP？

回答：不能。它只能说明某个设备节点输出了 NV12；RAW Bayer 到 DPHY、RKCIF、RKISP 的链路属于设备侧 BSP/硬件。当前仓库只实现用户态消费和验证工具，`docs/isp/13_ISP_IMPLEMENTATION_STATUS.md` 明确标记 RKISP/RKAIQ 未验证。

## 2. V4L2、vb2 与 Media Controller

`Video Node` 面向应用的帧队列；`Subdev Node` 面向 Sensor/CSI/ISP 的格式和控制；`Media Controller` 描述实体和 link。vb2 通常负责 buffer queue、状态和内存模式，应用通过 ioctl 操作它。

本项目的 buffer 证据：`include/camera_frame.h:22-63` 描述 slot、plane、stride、offset、timestamp 和 DMA-BUF fd；`BufferLeaseState:67-142` 用引用计数保证最后一个消费者释放后才 QBUF；`include/bounded_frame_queue.h:19-53` 提供有界 drop-oldest 队列。

面试回答要点：

- `DQBUF` 只代表驱动把 slot 交给用户态，不代表消费者已经读完。
- `QBUF` 太早会让驱动覆盖仍被 AI/编码读取的内存。
- `STREAMOFF` 之前要停止新帧、等待 lease 归零，再关闭 mmap/fd。
- Media graph 需要 `media-ctl -p` 和实体/link 证据，不能用 `/dev/video0` 名字猜 ISP。

## 3. MIPI CSI-2、DPHY、Sensor 与 DTS

MIPI CSI-2 负责 lane 上的数据包，DPHY 提供物理层，Sensor 通过 I2C 设置输出模式，DTS 连接 clock、GPIO、regulator 和 endpoint。`data-lanes`、`link-frequencies`、`remote-endpoint` 必须和原理图、mode table、板级 DPHY 一致。

当前仓库没有 `.dts/.dtsi` 和 Sensor driver。`docs/isp/patches/imx415-rk3588-overlay-template.dts` 是审查模板，不能直接应用。缺失的 I2C 地址、XCLK、reset/PWDN、lane 数、link frequency 和 IQ 必须在板端确认。

扩展学习：阅读目标 BSP 的 `drivers/media/i2c/imx415.c`、RK3588 media graph 和设备树 binding；不要把上游 Linux 参考驱动直接当成当前板卡实现。

## 4. RAW10/RAW12、Bayer 与 RKISP

RAW10 常见为 4 个像素打包成 5 bytes，RAW12 常见为 2 个像素打包成 3 bytes。Bayer pattern（RGGB/GRBG/GBRG/BGGR）决定 R、G、B 采样位置。Black level 是传感器/ISP 统计的基线。

源码：`scripts/isp_quality_analyzer.py:27-58` 解包 RAW10/12；`:212-240` 计算 Bayer plane、黑电平、R/G、B/G、直方图、噪声和边缘指标；NV12 只分析 Y 平面（`:243-259`）。

这些指标是离线分析，不是硬件 ISP 实现。不能把 RAW 解包说成完成 demosaic，也不能把亮度趋势说成实现 AE。

## 5. RKISP 与 rkAIQ

RKISP 负责 black level、demosaic、AWB/CCM、gamma、denoise、sharpen 等硬件处理；rkAIQ 通常根据 IQ 文件和 3A 统计更新 ISP 参数。AE 调 sensor exposure/gain，AWB 调 RGB gain/色彩参数，AF 依赖模组和硬件支持。

当前代码：`lib/aiq_controller.cpp:83-99` 仅发现路径/进程；`:102-124` 的 AE、曝光、gain、AWB 写操作全部返回 `Unsupported`。因此面试中应说“设计了 fail-closed 接入点”，不能说“已经调用 rkAIQ”。

## 6. 软件配帧与硬件同步

`lib/frame_pairer.cpp:61-67` 只在 clock domain 已知且相同的情况下比较 timestamp；`:88-160` 搜索容差内最优候选；`:162-194` 处理超时或 HoldLast。`lib/dual_camera_pipeline.cpp:392-393` 在没有 FSIN/Trigger backend 时对 hardware mode fail-fast。

软件配帧回答：使用内核 timestamp、sequence 和容差匹配两路已采集帧。硬件同步回答：需要 Sensor FSIN/XVS/XHS、板级连线、master/slave 配置和示波器/帧开始证据。时间戳相近不等于两颗 Sensor 同时曝光。

## 7. AE/AWB 面试边界

项目已有：离线亮度、Bayer 通道比例和帧稳定性指标。项目没有：rkAIQ AE/AWB context、3A statistics、曝光/gain/WB 回读和 IQ 生效证据。

正确回答范式：

> 我能解释 AE/AWB 的闭环、统计量和实验设计，也写了 RAW/NV12 离线分析；当前仓库没有目标 BSP/rkAIQ，所以没有把离线指标冒充成硬件控制结果。

## 8. 需要掌握的追问

- 为什么同一个 NV12 节点不能证明 RAW→ISP？
- 单平面 NV12 与双平面 NV12M 的 `data_offset/bytesused/stride` 如何计算？
- 为什么 Unknown clock domain 不能直接相减？
- 2 lane/4 lane、pixel rate、link frequency、FPS 如何通过 mode table 和带宽核对？
- IQ 文件、Sensor、镜头和 rkAIQ schema 为什么必须匹配？

这些问题的源码证据分别在 `lib/v4l2_isp_camera.cpp`、`lib/frame_pairer.cpp`、`docs/isp/01-04` 和 `docs/isp/13`。
