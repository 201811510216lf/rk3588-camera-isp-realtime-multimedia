# RK3588 硬件ISP采集接入实现

本轮基于 main=1119530。目标是完善现有 V4L2IspCamera 的真实Linux ioctl路径，让RKISP输出直接进入原DualCameraPipeline；不重写FramePairer、RKNN、RGA、MPP或FFmpeg，不改Software ISP算法，保留camera_isp_demo。

## 实际数据路径

Sensor → CIF/CSI/RKISP → 配置指定的V4L2 NV12/NV12M节点 → CameraFrame共享lease → 原录像/AI队列与FramePairer → 原RGA/OSD、MPP、FFmpeg。

当前使用驱动分配的MMAP Buffer。VIDIOC_EXPBUF是可选导出能力，失败保留MMAP采集；导出的FD尚未导入下游。NV12/NV12M按实际stride/bytesused打包为CPU连续NV12，再交原处理模块，不宣称端到端DMA-BUF或零拷贝。没有新增MJPEG解码、USB或文件输入。

## 必要源码改动

- V4L2IspCamera：按V4L2_CAP_DEVICE_CAPS选择能力字段，枚举格式/尺寸/帧间隔，S_FMT后G_FMT回读，保留G_PARM有理数帧间隔及field/sizeimage。记录可选枚举不支持，不把请求值当实际FPS。
- 采集与释放：保留MMAP+可选EXPBUF；核对QUERYBUF/DQBUF平面数、长度、偏移、payload和渐进帧；修复尾行padding判断与UV偏移溢出。启动失败锁定防止重复QBUF；poll/驱动错误和QBUF失败显式停止。共享lease最后释放才归还，停止等待消费者，未释放时保留映射供重试。
- 配置与Pipeline：仅--isp-config即可提供两路节点/尺寸/FPS/格式；未显式传入的全局默认值不再覆盖文件。显式冲突继续报错。每路按实际frame.desc解释NV12，Independent分别使用帧率，Joint要求可兼容的尺寸和实际帧间隔。
- ISP准备：复用AiqController与现有Coordinator。协商前核对Sensor/ISP/node映射与所有权；活动输出的最终检查必须在S_FMT/G_FMT之后重新读取，不把准备阶段当完整输出确认。
- 新isp_capture只是正式类的有界单路诊断入口，保存紧凑NV12与逐帧元数据，方便先单路后双路验收。

## RkAiq真实依赖

本仓库没有提供目标RKAIQ SDK头/库/核验port，本轮构建的RKAIQ_ROOT、RKAIQ_INCLUDE_DIR、RKAIQ_LIBRARIES、RKAIQ_ADAPTER_SOURCE未配置。现有CMake最小接入保留；不新增Fake Backend或猜测rk_aiq函数。

External Service模式不执行init/start/stop/deinit；进程被发现不等于Sensor绑定、IQ加载、AE/AWB已确认，无已核验IPC就保持未知/不支持。In-process只有匹配BSP头/库和经官方示例核验的adapter才能运行，仍需所有权检查及BSP启停顺序。两个Sensor不代表两个独立ISP；共享资源/Camera Group须由目标BSP确定，没有证据不创建两个独立context。

源码实现、主机Linux编译、真实SDK编译、板端实测分别记录。后两者当前BLOCKED_HARDWARE。构建/操作见[采集说明](RK3588_ISP_CAPTURE.md)、[双路说明](RK3588_DUAL_CAMERA.md)，实际结果见[测试报告](RK3588_ISP_TEST_REPORT.md)。

## 参考依据

仅参考[RK-MediaProject](https://github.com/JoeChen2me/RK-MediaProject)的Camera职责、ioctl和线程/队列管理方式，没有复制其USB MJPEG业务或第三方二进制。API行为核对[Linux MMAP文档](https://docs.kernel.org/userspace-api/media/v4l/mmap.html)、[S_FMT/G_FMT文档](https://docs.kernel.org/userspace-api/media/v4l/vidioc-g-fmt.html)。
