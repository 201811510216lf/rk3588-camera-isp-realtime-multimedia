# Camera / ISP 岗位简历版本

## 证据边界

本版本只把当前工作树中可定位到的用户态源码写入简历。`V4L2IspCamera` 已实现 NV12/NV12M 的 capability、格式协商、MMAP、DQBUF/QBUF、stride/bytesused/timestamp 校验；仓库没有 Sensor driver、DTS/DTSI、Media Controller 配置、IQ 文件或真实 rkAIQ 运行库。当前 `tests/validate_project.py` 和 Python 静态检查可通过，但 CMake/C++/CTest 与 RK3588 板端均未验证。

## 项目名称与技术栈

**双路 RK3588 MIPI Camera 用户态采集、软件配帧与 AI 视频流水线**
技术栈：C++17、Linux V4L2、V4L2 MMAP、NV12/NV12M、FramePairer、RGA/OpenCV、RKNN、MPP H.264、线程安全有界队列。

## 80～120 字项目描述（当前已验证版）

面向 RK3588 双路 MIPI Camera 场景，搭建两路 V4L2 ISP 输出采集、驱动时间戳软件配帧、双路 AI 推理、RGA/CPU 拼接与单路 MPP H.264 编码的用户态流水线。重点处理多平面 NV12 布局、Camera Buffer Lease、队列丢帧和停止回收；当前完成源码级实现与静态检查，板端 Sensor/ISP/rkAIQ 和性能数据待补。

## 四条核心技术工作

1. 在 `lib/v4l2_isp_camera.cpp:203-483,484-505,506-698` 完成 V4L2 capability/format、REQBUFS、MMAP、poll/DQBUF、plane offset/bytesused/stride/timestamp 校验及安全停止；通过 `include/camera_frame.h:76-167` 让最后一个 lease 释放时 QBUF。
2. 在 `lib/frame_pairer.cpp:30-253` 按相同 clock domain 和时间戳容差选择最小 delta 配对，支持 `drop-older`/`hold-last`，记录丢帧与 P50/P95/P99；这是软件配帧，不等价于硬件同步曝光。
3. 在 `lib/dual_camera_pipeline.cpp:238-317,319-364` 将两路帧分别送入有界 AI 队列，复制 NV12 后用 RGA/OpenCV 转换、运行 RKNN、按 sequence/timestamp 做 OSD 可用性判断，再完成左右拼接。
4. 在 `lib/dual_camera_pipeline.cpp:366-380` 统一提交一个 MPP 编码器，并通过 `lib/encoded_packet_fanout.cpp:5-62` 把共享 H.264 包发送到 raw/RTMP/MP4 sink；当前网络/磁盘写入仍在编码线程同步执行，不能表述为完全异步。

## 面试追问与回答边界

| 追问 | 基于证据的回答 |
|---|---|
| 你写过 Sensor 驱动吗？ | 当前仓库没有 Sensor 驱动和 DTS 源码；我负责的是用户态 V4L2 ISP 输出接入和帧生命周期。Sensor/CSI/ISP 需要依赖目标 BSP 的已有链路，尚未完成板端证据闭环。 |
| 这是零拷贝吗？ | 不是完整零拷贝。V4L2 侧是 MMAP，`EXPBUF` 只导出 fd；`copy_nv12` 会把相机 buffer 拷到独立 packed 缓冲，再给 RGA/MPP/RKNN 使用。DMA-BUF import、cache/fence 尚未验证。 |
| 两路真的硬件同步吗？ | 代码只使用 V4L2 驱动时间戳做软件配帧；`--sync-mode hardware` 没有已验证 FSIN/Trigger backend 时 fail-fast。不能宣称同时曝光。 |
| rkAIQ 调用成功了吗？ | `lib/aiq_controller.cpp` 的 AE/AWB/曝光/增益写接口是 unsupported fail-closed；没有真实 rkAIQ API、IQ 文件和回读日志。 |

## 目标完成后版本（仅草稿）

目标板完成 Media Graph、Sensor/DTS、IQ/rkAIQ、同步触发和真实采集后，才可把“适配双路 Sensor”“验证 AE/AWB”和“硬件同步”加入简历。完成前不得使用这些表述。

## 对应证据文件

`main.cpp:19-101`、`CMakeLists.txt:13-130`、`lib/v4l2_isp_camera.cpp`、`include/camera_frame.h`、`lib/frame_pairer.cpp`、`lib/dual_camera_pipeline.cpp`、`docs/BSP_CAMERA_ISP.md`、`docs/BOARD_ACCEPTANCE.md`、`tests/frame_pairer_test.cpp`。

