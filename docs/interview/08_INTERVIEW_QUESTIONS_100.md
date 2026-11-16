# 100 道项目相关面试题与答案

答案标签：SOURCE=源码可定位；HOST_STATIC=结构/Python检查；HOST_CXX_NOT_RUN=当前无 CMake/编译器；BOARD_NOT_VERIFIED=无 RK3588/BSP/设备/模型/服务器证据。

## Camera/ISP（25 题）

### 1. Sensor→CSI→ISP→V4L2 关系

- **考察：** Sensor→CSI→ISP→V4L2 关系 的工作原理、边界和验证方法。
- **标准答案：** Sensor 通过 MIPI CSI-2 输出 RAW，CSI/RKCIF 接收后由 RKISP 做 3A、去马赛克和色彩处理，最后由 V4L2 video node 输出 NV12/NV12M。当前仓库只接收最后的 video node。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 2. V4L2 输出 NV12 是否等于实现 ISP

- **考察：** V4L2 输出 NV12 是否等于实现 ISP 的工作原理、边界和验证方法。
- **标准答案：** 不等于。NV12 可能是 BSP ISP 已处理的输出，当前代码只做 capability/format、MMAP、DQBUF/QBUF 和布局校验；仓库没有 Sensor/DTS/ISP 驱动。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 3. video node 与 subdev node

- **考察：** video node 与 subdev node 的工作原理、边界和验证方法。
- **标准答案：** video node 面向帧 I/O；subdev 面向 Sensor/CSI/ISP 的 pad、格式和控制。当前只打开 video node。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 4. Media Controller

- **考察：** Media Controller 的工作原理、边界和验证方法。
- **标准答案：** 用 entity/pad/link 描述媒体拓扑和格式传播；media_graph_validator 仅只读解析，不修改链路。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 5. vb2

- **考察：** vb2 的工作原理、边界和验证方法。
- **标准答案：** videobuf2 管理内核侧 buffer 队列、内存类型和状态；当前仓库只通过 ioctl 使用，不包含 vb2 内核实现。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 6. MIPI CSI-2 帧边界

- **考察：** MIPI CSI-2 帧边界 的工作原理、边界和验证方法。
- **标准答案：** 硬件按包和虚拟通道识别帧，驱动向用户态提供 sequence/timestamp；sequence 不等于曝光时刻。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 7. DPHY

- **考察：** DPHY 的工作原理、边界和验证方法。
- **标准答案：** MIPI 物理层负责差分电气、时钟恢复和 lane 状态，仓库没有 DPHY 驱动。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 8. RAW10/12

- **考察：** RAW10/12 的工作原理、边界和验证方法。
- **标准答案：** RAW10/12 是 Bayer 原始采样，需 ISP 解码；NV12 是 YUV420 半平面输出，二者不能直接当同一格式。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 9. Bayer pattern

- **考察：** Bayer pattern 的工作原理、边界和验证方法。
- **标准答案：** RGGB/BGGR 等排列决定通道对应，配置错误会造成颜色异常；当前仓库没有 Bayer 配置。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 10. AE/AWB/AF

- **考察：** AE/AWB/AF 的工作原理、边界和验证方法。
- **标准答案：** AE 调曝光/增益，AWB 调白平衡，AF 调焦；当前 rkAIQ 控制接口是 unsupported，未完成回读验证。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 11. IQ 文件

- **考察：** IQ 文件 的工作原理、边界和验证方法。
- **标准答案：** ISP tuning 参数，需匹配 Sensor、模式和版本；当前仓库无 IQ 文件和匹配日志。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 12. rkAIQ 边界

- **考察：** rkAIQ 边界 的工作原理、边界和验证方法。
- **标准答案：** rkAIQ 是用户态算法/控制框架，驱动和 ISP 硬件在 BSP；本项目仅有 fail-closed 控制壳。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 13. 软件配帧

- **考察：** 软件配帧 的工作原理、边界和验证方法。
- **标准答案：** 两路按 camera_id、timestamp、clock_domain 放入有界队列，在容差内选最小 delta；FramePairer 支持 drop-older/hold-last。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 14. 时间戳不等于曝光同步

- **考察：** 时间戳不等于曝光同步 的工作原理、边界和验证方法。
- **标准答案：** timestamp 可能是 SOF/EOF 或取数时间，两 Sensor 时钟和曝光起点也可能不同；需要共享触发、SOF/示波器证据。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 15. drop-older/hold-last

- **考察：** drop-older/hold-last 的工作原理、边界和验证方法。
- **标准答案：** drop-older 只输出新鲜配对；hold-last 允许旧帧并标 stale，适合展示降级。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 16. clock domain

- **考察：** clock domain 的工作原理、边界和验证方法。
- **标准答案：** 不同时间基不能直接比较；未知或不一致时拒绝配对，避免假同步。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 17. Sensor bring-up

- **考察：** Sensor bring-up 的工作原理、边界和验证方法。
- **标准答案：** 确认供电/时钟/reset/I2C，检查 probe、media graph、格式路由，再单路抓帧和曝光控制；本仓库只提供探测脚本。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 18. RKCIF/RKISP

- **考察：** RKCIF/RKISP 的工作原理、边界和验证方法。
- **标准答案：** RKCIF 接收/搬运 CSI 数据，RKISP 做图像处理，video node 是输出；具体实体需看 BSP graph。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 19. 两路格式协商

- **考察：** 两路格式协商 的工作原理、边界和验证方法。
- **标准答案：** 分别读取两路实际 width/height/fps 并检查一致，当前 run 对 capabilities 做相等性检查。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 20. timestamp flags

- **考察：** timestamp flags 的工作原理、边界和验证方法。
- **标准答案：** 说明 timestamp 来源和时钟域；代码保存这些字段，但没有板端语义验证。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 21. poll timeout

- **考察：** poll timeout 的工作原理、边界和验证方法。
- **标准答案：** 让线程周期性检查 stop 和 pairer.tick，避免 DQBUF 永久阻塞。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 22. mainpath/selfpath

- **考察：** mainpath/selfpath 的工作原理、边界和验证方法。
- **标准答案：** ISP 不同输出通路，可有不同分辨率/格式；当前没有固定节点证据。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 23. Camera 贡献边界

- **考察：** Camera 贡献边界 的工作原理、边界和验证方法。
- **标准答案：** 可声称用户态 V4L2、配帧、lease 和错误处理；不能声称 Sensor/DTS/rkAIQ/硬件同步。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 24. 硬件同步证据

- **考察：** 硬件同步证据 的工作原理、边界和验证方法。
- **标准答案：** 需要 FSIN/Trigger、电平/时钟、driver 寄存器、启动顺序以及 SOF/示波器数据；当前 hardware 模式 fail-fast。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 25. ISP 调试顺序

- **考察：** ISP 调试顺序 的工作原理、边界和验证方法。
- **标准答案：** 先 media-ctl/v4l2-ctl 保存 graph 和能力，再单路抓帧、检查 stride/timestamp，最后做 AI/编码集成；当前未上板。
- **本项目例子：** main.cpp、lib/v4l2_isp_camera.cpp、lib/frame_pairer.cpp、docs/BSP_CAMERA_ISP.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

## V4L2/DMA-BUF/RGA（20 题）

### 26. MMAP vs DMA-BUF

- **考察：** MMAP vs DMA-BUF 的工作原理、边界和验证方法。
- **标准答案：** MMAP 让应用映射驱动分配的 buffer；DMA-BUF 通过 fd 在 V4L2、RGA、MPP 间传递同一块内存。当前代码用 MMAP + `copy_nv12()`，`VIDIOC_EXPBUF` 只记录 fd，没有把 fd 导入 RGA/MPP，因此不能称为零拷贝。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 27. copy_nv12 stride

- **考察：** copy_nv12 stride 的工作原理、边界和验证方法。
- **标准答案：** `copy_nv12()` 以 `stride_y/stride_uv` 作为源行步长，以 width 作为 packed 目标行步长，逐行复制 Y 和 UV，并检查 `data_length`。这样能去掉 padding，但会产生一次 CPU copy。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 28. NV12 布局

- **考察：** NV12 布局 的工作原理、边界和验证方法。
- **标准答案：** NV12 是 Y 平面后接交错 UV，大小通常为 Y 的 1.5 倍；读取时还要考虑 stride、offset 和 bytesused。项目把驱动布局转换成连续 packed NV12，再交给 RGA/OpenCV/MPP。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 29. data_offset

- **考察：** data_offset 的工作原理、边界和验证方法。
- **标准答案：** `data_offset` 表示有效像素不一定从 mmap 起始地址开始。`dequeue()` 将 offset 加到映射地址前先检查 offset 不超过 plane 长度，避免把头部元数据或越界区域当作图像。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 30. bytesused/sizeimage/mmap length

- **考察：** bytesused/sizeimage/mmap length 的工作原理、边界和验证方法。
- **标准答案：** `sizeimage` 是驱动为 plane 预留的容量，`bytesused` 是本帧有效 payload，mmap length 是映射范围；三者不能混用。当前代码在 QUERYBUF 和 DQBUF 处检查它们，`copy_nv12()` 只读有效长度。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 31. cache coherency

- **考察：** cache coherency 的工作原理、边界和验证方法。
- **标准答案：** CPU 与设备共享缓存时，必须确认 cache flush/invalidate 和 fence 语义；仅有同一个 fd 不代表可见性正确。仓库走 CPU packed copy，刻意避开尚未确认的 DMA-BUF cache/fence 契约，板端仍需验证。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 32. RGA 用途

- **考察：** RGA 用途 的工作原理、边界和验证方法。
- **标准答案：** RGA 是硬件 2D 引擎，适合颜色转换和 resize；双路 pipeline 在 `nv12_to_bgr_rga()` 和 compose 的 `imresize()` 处调用，失败时回退 OpenCV。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 33. RGA vs CPU

- **考察：** RGA vs CPU 的工作原理、边界和验证方法。
- **标准答案：** RGA 能降低 CPU 参与，但依赖格式、stride、地址和同步契约；CPU/OpenCV 更容易验证但占用 CPU。项目选择 RGA 优先、OpenCV fallback，并没有 benchmark 证明哪条更快。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 34. DMA-HEAP

- **考察：** DMA-HEAP 的工作原理、边界和验证方法。
- **标准答案：** DMA-HEAP 是 Linux 分配可共享 DMA buffer 的接口，通常与 dma-buf fd、IOMMU 和 cache 语义一起使用。当前仓库没有 DMA-HEAP 分配器，`EXPBUF` 也不等于 DMA-HEAP。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 35. ROI 越界

- **考察：** ROI 越界 的工作原理、边界和验证方法。
- **标准答案：** ROI 必须满足源图范围、目标矩形和偶数对齐等约束；越界会导致 RGA 失败或写坏内存。`CompositeProcessor::clip_box()` 在 OSD 前裁剪框，compose 还按 layout 生成合法 ROI。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 36. OpenCV fallback

- **考察：** OpenCV fallback 的工作原理、边界和验证方法。
- **标准答案：** RGA 转换失败时用 OpenCV 保底，保证流程仍能输出，但这会改变 CPU 负载和延迟。该分支存在源码路径，尚未在真实 RK3588 格式/stride 上验证。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 37. QBUF 失败

- **考察：** QBUF 失败 的工作原理、边界和验证方法。
- **标准答案：** QBUF 失败不能静默吞掉，否则驱动 slot 会长期处于 in-flight。当前 `BufferLeaseState::release()` 捕获异常，实际 QBUF 路径记录错误；应结合 `camera.stop()` 和计数器检查是否最终回收。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 38. 非法 DQBUF index

- **考察：** 非法 DQBUF index 的工作原理、边界和验证方法。
- **标准答案：** DQBUF 返回的 index 必须小于 slot 数且对应已排队状态。当前 `dequeue()` 同时检查越界和 slot 状态，异常时停止流并返回错误；测试源码有覆盖接口，但 C++ 未执行。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 39. 重复 slot

- **考察：** 重复 slot 的工作原理、边界和验证方法。
- **标准答案：** 同一个 slot 在旧 lease 未释放时再次 DQBUF 会造成两个所有者和重复 QBUF。当前代码在 DQBUF 前检查 `BufferSlotState::Queued`，发现重复状态即停流；真实异常驱动注入仍待板测。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 40. timestamp source flags

- **考察：** timestamp source flags 的工作原理、边界和验证方法。
- **标准答案：** timestamp source flags 说明时间戳来自哪种内核时钟/来源，`clock_domain` 则用于判断两路是否可比较。项目保留 flags 并拒绝 Unknown 或不相等 domain 的配对，但没有证明驱动语义是 SOF 还是 EOF。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 41. poll revents

- **考察：** poll revents 的工作原理、边界和验证方法。
- **标准答案：** poll 返回后应检查 `revents`，区分 POLLIN、POLLERR、POLLHUP；只看返回值可能把设备错误当成无数据。当前路径主要依赖 poll/DQBUF 错误文本，`revents` 细化审计仍是待改进点。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 42. mmap 生命周期

- **考察：** mmap 生命周期 的工作原理、边界和验证方法。
- **标准答案：** 每个 plane 的 mmap 地址和长度必须与 QUERYBUF 对应，停止时按同一长度 munmap。`V4L2IspCamera` 在 shutdown 中逐 plane 释放；若驱动返回异常 plane 数，应拒绝而不是忽略额外 plane。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 43. format 回读

- **考察：** format 回读 的工作原理、边界和验证方法。
- **标准答案：** 格式协商要先确认 capability，再对 multi-planar NV12/NV12M 设置 width、height、pixelformat 和 field，最后读取实际结果。当前 `open()` 依次尝试 NV12 multi-planar、NV12M/单平面，并在 run 中比较两路实际能力。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 44. NV12/NV12M 混用

- **考察：** NV12/NV12M 混用 的工作原理、边界和验证方法。
- **标准答案：** NV12 与 NV12M 的差异是 plane 数和地址布局，不是颜色格式本身。应用应按 `plane_count` 选择 Y/UV 指针；项目同时保存两类元数据，但异常 3+ plane 的严格拒绝仍应补强。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 45. DMA-BUF import 证据

- **考察：** DMA-BUF import 证据 的工作原理、边界和验证方法。
- **标准答案：** 要证明 DMA-BUF import，至少需要下游 API 接收 fd、cache/fence 处理、生命周期日志和板端压力结果。当前只有 `VIDIOC_EXPBUF` 导出记录，RGA/MPP/RKNN 仍消费 CPU packed copy，所以证据不足。
- **本项目例子：** lib/v4l2_isp_camera.cpp、camera_frame.h、dual_camera_pipeline.cpp、composite_processor.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

## MPP/H264/RTMP/MP4（15 题）

### 46. MPP 调用链

- **考察：** MPP 调用链 的工作原理、边界和验证方法。
- **标准答案：** MPP 编码器的流程是创建 context、设置 prep/rc/codec 配置、申请输入/输出 buffer、提交 YUV frame、取 packet。`MppEncoder::Init/Encode/GetHeader` 对应这些步骤，双路拼接后只调用一个 encoder。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 47. SPS/PPS

- **考察：** SPS/PPS 的工作原理、边界和验证方法。
- **标准答案：** SPS/PPS 描述解码参数，通常作为 H.264 extra info 在码流前发送。`GetHeader()` 取 MPP extra info，run 时写入 raw、RTMP、MP4 sink；没有 ffprobe/播放器结果，不能写成可播放已验证。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 48. IDR/P 帧

- **考察：** IDR/P 帧 的工作原理、边界和验证方法。
- **标准答案：** IDR 是可独立解码的关键帧，P 帧依赖参考帧；GOP 决定关键帧间隔。当前 MPP 配置 `rc:gop=fps`，`has_h264_idr()` 只用于标记 packet，实际 GOP/码流仍需板端检查。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 49. Annex-B/AVCC

- **考察：** Annex-B/AVCC 的工作原理、边界和验证方法。
- **标准答案：** Annex-B 使用 start code 分隔 NAL，AVCC 使用长度前缀并在容器中保存 extradata。当前 raw sink 直接写 MPP 输出的 Annex-B 假设，ffmpeg 管道也以 h264 输入；是否与目标 MPP 输出一致需用 hexdump/ffprobe验证。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 50. PTS/DTS

- **考察：** PTS/DTS 的工作原理、边界和验证方法。
- **标准答案：** PTS 是展示时间，DTS 是解码时间，编码包必须单调且通常 DTS 不大于 PTS。`EncodedPacketDistributor::publish()` 拒绝倒退时间戳；当前 encode_loop 用递增计数作 PTS，精确时间基未完成板端验证。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 51. 单编码器原因

- **考察：** 单编码器原因 的工作原理、边界和验证方法。
- **标准答案：** 编码器内部通常有输入队列、参考帧和输出 packet，提交成功不一定意味着输入 buffer 已经不再被硬件访问。应用要以 SDK 的 completion/flush 语义为准，不能只看到 `encode_put_frame` 返回成功。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 52. MPP timeout 释放

- **考察：** MPP timeout 释放 的工作原理、边界和验证方法。
- **标准答案：** 发生 timeout 或错误时，不能立即 `mpp_buffer_put()`，因为硬件可能仍在读。当前实现把 pending buffer 保留到 `Release()` 销毁 context，属于保守策略；目标 MPP 的 reset/flush 完成点未验证。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 53. 空 packet vs 硬件错误

- **考察：** 空 packet vs 硬件错误 的工作原理、边界和验证方法。
- **标准答案：** 空 packet 可能表示编码延迟，也可能是硬件错误，必须结合返回码、状态和后续 flush 区分。当前 `Encode()` 对无 packet 返回失败，pipeline 记 `encode_errors` 并停止，仍需板端确认语义。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 54. RTMP/MP4 共用码流

- **考察：** RTMP/MP4 共用码流 的工作原理、边界和验证方法。
- **标准答案：** RTMP 是实时传输协议路径，MP4 是文件封装路径；二者都可以消费同一份 H.264 NAL，但时间戳和封装要求不同。项目通过 `FfmpegPipe` 把同一 `EncodedPacket` 写到两个 ffmpeg 进程，真实服务器/文件未验证。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 55. segment muxer

- **考察：** segment muxer 的工作原理、边界和验证方法。
- **标准答案：** segment muxer 按时间切分输入并生成多个 MP4 文件，切点通常受关键帧影响。当前命令使用 `-f segment -segment_time ... -segment_format mp4`，是否能正常封装要用 ffprobe 检查分段文件。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 56. 断线重连

- **考察：** 断线重连 的工作原理、边界和验证方法。
- **标准答案：** 网络抖动会导致 ffmpeg stdin 写阻塞或失败，磁盘满会使 MP4 写失败；当前 `FfmpegPipe::write()` 会关闭并重开一次命令并统计错误，但 sink 仍同步阻塞编码线程。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 57. 编码 vs 封装

- **考察：** 编码 vs 封装 的工作原理、边界和验证方法。
- **标准答案：** 编码是把图像压成 H.264 NAL，封装是把 NAL 与时间戳放进 FLV/MP4 等容器。项目先由 MPP 生成码流，再由 ffmpeg 负责 RTMP/MP4 封装，没有把二者混为一个步骤。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 58. raw H264

- **考察：** raw H264 的工作原理、边界和验证方法。
- **标准答案：** raw H.264 适合验证 NAL 和播放器兼容性，不包含 MP4 索引等容器信息。`raw_output` 在 run 中写 SPS/PPS 和每个 packet，写入失败应检查 stream 状态；当前未执行真实播放。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 59. fan-out 代价

- **考察：** fan-out 代价 的工作原理、边界和验证方法。
- **标准答案：** fan-out 的价值是一次编码、多个消费者；`EncodedPacket` 用 `shared_ptr<const vector<uint8_t>>` 共享不可变 payload，distributor 逐 sink 调用并累计错误。它不自动提供异步队列，当前慢 sink 仍会阻塞 encode loop，`publish(false)` 也未完整传播为 pipeline failure。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 60. 端到端视频验证

- **考察：** 端到端视频验证 的工作原理、边界和验证方法。
- **标准答案：** 端到端视频验证要分层：先单路 NV12，再拼接/OSD，再检查 H.264 header/NAL，再 ffprobe MP4，最后连 RTMP 服务器看拉流和重连。仓库提供验收清单，但当前没有板端和服务器证据。
- **本项目例子：** lib/mpp_encoder.cpp、dual_camera_pipeline.cpp、encoded_packet_fanout.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

## RKNN/AI（10 题）

### 61. RKNN context

- **考察：** RKNN context 的工作原理、边界和验证方法。
- **标准答案：** RKNN context 包含模型、输入输出属性和 NPU 运行状态；双路各自创建 `rknn_lite`，AI 线程分别处理 camera0/1。当前模型路径可选，`ai_required` 决定失败是否终止 pipeline。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 62. core mask

- **考察：** core mask 的工作原理、边界和验证方法。
- **标准答案：** 仓库的 `rknnPool.hpp` 确实调用 `rknn_set_core_mask()`，但双路入口在 `dual_camera_pipeline.cpp:435` 对两个 context 都传 `n=0`，CLI 的 core 参数只打印 warning。因此不能写“已实现 core0/core1 绑定”。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 63. NHWC/NCHW

- **考察：** NHWC/NCHW 的工作原理、边界和验证方法。
- **标准答案：** NCHW 是 C×H×W，NHWC 是 H×W×C；模型属性决定输入布局，`rknn_input.fmt` 必须一致。当前 wrapper 能按属性读取尺寸，却无条件把输入 fmt 设成 NHWC，NCHW 模型存在正确性风险。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 64. INT8 量化

- **考察：** INT8 量化 的工作原理、边界和验证方法。
- **标准答案：** INT8 量化把浮点值映射为整数，反量化常用 `real=(q-zp)*scale`。wrapper 查询 output scale/zp 并传给 YOLO 后处理；具体量化误差和模型精度没有数据，属于模型验证缺口。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 65. YOLO 后处理

- **考察：** YOLO 后处理 的工作原理、边界和验证方法。
- **标准答案：** YOLO 后处理通常把多尺度输出解码成框、类别和置信度，再做阈值过滤。`postprocess.cpp` 固定三尺度输出、anchors 和类别配置，结果被转换为 `DetectionBox`；仓库没有模型文件，不能证明业务类别正确。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 66. NMS

- **考察：** NMS 的工作原理、边界和验证方法。
- **标准答案：** NMS 按 IoU 抑制同类重叠框，保留置信度更高者。当前后处理实现包含 NMS，但审查发现候选类别索引使用存在待测风险，需用标注样本回归，不能只凭能运行就宣称正确。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 67. Letterbox

- **考察：** Letterbox 的工作原理、边界和验证方法。
- **标准答案：** Letterbox 先按比例缩放再补边，推理框要用同一缩放和偏移映射回原图。当前 compose 使用 layout 映射框并裁剪；wrapper 内部 resize 与外部 layout 的联合精度需要模型样本验证。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 68. AI 结果关联

- **考察：** AI 结果关联 的工作原理、边界和验证方法。
- **标准答案：** AI 结果必须携带 camera_id、sequence 和 capture timestamp，OSD 只使用不晚于当前 display frame 的 snapshot。当前 `AiSnapshot` 保存这些字段，Strict 模式等待同 sequence，超时则不画框或显示 AI_WAIT。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 69. 同步推理瓶颈

- **考察：** 同步推理瓶颈 的工作原理、边界和验证方法。
- **标准答案：** 同步瓶颈包括 RGA/CPU 转换、RKNN 推理、OSD 和编码；capture 不能无限等待。项目让 AI 队列深度为 2、满载丢旧，并用 `simulate_ai_ms` 构造慢消费者；但同步 fwrite 仍会卡住 encode 线程。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 70. 模型不存在

- **考察：** 模型不存在 的工作原理、边界和验证方法。
- **标准答案：** 模型部署前要固定输入尺寸、颜色顺序、tensor fmt/type、输出数量、量化参数和类别标签，并用离线样本对比。wrapper 要求 1 个输入、至少 3 个输出；仓库没有 `.rknn` 模型和实测输出，部署只能标未验证。
- **本项目例子：** rknnPool.hpp、postprocess.cpp、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

## C++/Linux/并发（20 题）

### 71. RAII

- **考察：** RAII 的工作原理、边界和验证方法。
- **标准答案：** RAII 用对象生命周期绑定资源释放，例如 `DualCameraPipeline` 析构会 request_stop、join、关闭队列并 stop camera，`FrameLease` 析构触发 release。它减少异常路径遗漏，但不能替代 SDK completion 语义。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 72. unique/shared_ptr

- **考察：** unique/shared_ptr 的工作原理、边界和验证方法。
- **标准答案：** `unique_ptr` 表示 pipeline/model 的唯一所有权，`shared_ptr` 用于 CameraState 与 lease 跨线程共享；FrameLease 本身不可拷贝但由 `shared_ptr<FrameLease>` 持有。公开 owner/lease 仍允许构造不完整组合，是 API 风险。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 73. atomic stop

- **考察：** atomic stop 的工作原理、边界和验证方法。
- **标准答案：** atomic stop 只提供可见的停止标志，不能中断阻塞的 RKNN 或 `fwrite`。当前 `request_stop()` 还关闭队列并让 poll 通过超时返回；最终仍要 join，SDK 卡住时退出无界。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 74. condition_variable

- **考察：** condition_variable 的工作原理、边界和验证方法。
- **标准答案：** condition_variable 用于队列 pop 和 AI Strict 等待，必须在 mutex 保护的谓词上循环检查，以防虚假唤醒。`BoundedFrameQueue::pop()` 等待 closed 或非空，关闭时唤醒消费者并释放剩余对象。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 75. 有界队列

- **考察：** 有界队列 的工作原理、边界和验证方法。
- **标准答案：** 有界队列用固定容量控制内存和延迟，满载时选择丢旧、阻塞或丢新。项目 AI 队列容量 2、pair queue 容量 4，`push_drop_oldest()` 计数并释放被丢弃的 lease；编码/网络支路仍有同步长尾。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 76. drop-oldest

- **考察：** drop-oldest 的工作原理、边界和验证方法。
- **标准答案：** drop-oldest 适合实时预览，因为新帧比旧帧更有价值；代价是可能跳帧。项目对 AI 队列和 pair queue 使用该策略并记录 `ai*_drops/pair_drops`，录像需求若要求完整性则应采用独立持久化队列。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 77. 生产者消费者

- **考察：** 生产者消费者 的工作原理、边界和验证方法。
- **标准答案：** 生产者是两个 capture loop，消费者是两个 AI loop 和一个 encode loop；队列负责所有权转移和唤醒。capture 不直接调用 RKNN/MPP，但每个 `CameraFrame` 的 lease 要等所有副本释放才能 QBUF。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 78. 死锁排查

- **考察：** 死锁排查 的工作原理、边界和验证方法。
- **标准答案：** 排查死锁先画锁顺序，再用线程栈和超时定位；不要在持有 queue/AI mutex 时调用外部 SDK。当前代码分别使用 queue、ai、stats、events mutex，`rknn_lite::interf()` 在释放 frame 后调用，仍需压力测试验证。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 79. 数据竞争

- **考察：** 数据竞争 的工作原理、边界和验证方法。
- **标准答案：** 数据竞争的典型来源是 counters、AI snapshot、stop 和 queue 状态。项目用 atomic stop、`stats_mutex`、`ai_mutex` 和队列内部 mutex；但 C++ 未编译，需 ThreadSanitizer/长稳测试才能形成证据。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 80. pipeline 禁拷贝

- **考察：** pipeline 禁拷贝 的工作原理、边界和验证方法。
- **标准答案：** pipeline 禁拷贝可以防止 fd、线程、MPP context 被复制造成双重释放。`DualCameraPipeline` 明确 delete copy constructor/assignment，`rknn_lite` 和 queue 也禁拷贝；移动语义未提供，实例按单对象使用。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 81. 单次 run

- **考察：** 单次 run 的工作原理、边界和验证方法。
- **标准答案：** 当前 `DualCameraPipeline::run()` 用 `run_started` 拒绝同一实例第二次运行，避免队列和状态重用错误。要重复运行应重新构造对象；这是源码约束，不是已执行测试结果。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 82. join 风险

- **考察：** join 风险 的工作原理、边界和验证方法。
- **标准答案：** join 能保证线程退出后再释放其依赖资源，但如果线程卡在 RKNN 或同步 sink 就会一直等待。当前析构和 run 都按 capture→AI→encode 顺序 join，并先 close queue；需要补充超时/进程隔离策略。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 83. ioctl 错误

- **考察：** ioctl 错误 的工作原理、边界和验证方法。
- **标准答案：** ioctl 返回值和 errno 是驱动错误定位的核心；封装的 `xioctl()` 处理 EINTR，调用点把 QUERYCAP、S_FMT、REQBUFS、DQBUF、STREAMON/OFF 失败转成错误字符串。真实设备错误码尚未采集。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 84. mmap/ioctl

- **考察：** mmap/ioctl 的工作原理、边界和验证方法。
- **标准答案：** mmap 建立用户态地址映射，ioctl 配置和交换 buffer；二者必须按同一 plane 的 offset/length 配对，停止时先停止流再 munmap。项目的 shutdown 顺序遵循这一原则。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 85. 异常安全

- **考察：** 异常安全 的工作原理、边界和验证方法。
- **标准答案：** 异常安全要求错误路径也能释放 fd、mmap、线程和 lease。项目大量使用 RAII、unique_ptr 和析构 stop，但 `FrameLease` 的 QBUF 错误只能记录不能抛出；MPP/文件最终 flush 仍需额外检查。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 86. 多个 mutex

- **考察：** 多个 mutex 的工作原理、边界和验证方法。
- **标准答案：** 多个 mutex 时要建立全局锁顺序，避免交叉获取。当前主要锁是 pairer 内部、queue 内部、ai_mutex、stats_mutex、events_mutex；设计上不应在 stats 锁内调用 SDK，仍需动态工具验证。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 87. 背压

- **考察：** 背压 的工作原理、边界和验证方法。
- **标准答案：** 背压是消费者变慢时限制生产速率或丢弃策略；项目通过有界队列和 drop-oldest 防止 AI 任务无限积压，但同步 RTMP/MP4 写仍把背压传回 encode loop，最终可能占满 camera lease。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 88. 资源泄漏验证

- **考察：** 资源泄漏验证 的工作原理、边界和验证方法。
- **标准答案：** 验证泄漏要观察 fd/mmap、in-flight lease、队列深度、MPP buffer 和 ffmpeg 子进程，并重复启动/停止。当前只有测试源码和静态检查，没有 ASAN、valgrind、板端长稳或真实 fd 计数。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 89. 可观测统计

- **考察：** 可观测统计 的工作原理、边界和验证方法。
- **标准答案：** 可观测性至少包括每路 frame、pair、drop、AI、encode、sink error、delta P95/P99 和资源状态。`DualCameraPipelineStats` 与 FramePairerStats 已有字段，但性能文件标为 NOT_MEASURED，且部分错误需检查最终 flush。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

### 90. 停止协议

- **考察：** 停止协议 的工作原理、边界和验证方法。
- **标准答案：** 停止协议应先阻止新任务，再关闭队列唤醒消费者，等待线程 join，最后释放 encoder、sink、camera。当前析构/run 实现了这一顺序；同步 SDK 调用无法被 stop 强制打断，是退出风险。
- **本项目例子：** bounded_frame_queue.h、camera_frame.h、dual_camera_pipeline.cpp；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P1/P2

## 系统设计/性能（10 题）

### 91. 双路 vs USB

- **考察：** 双路 vs USB 的工作原理、边界和验证方法。
- **标准答案：** 双路 MIPI 适合固定 Sensor、低延迟和 ISP/同步控制；USB Camera 部署简单但受 USB 带宽、驱动时间戳和同步能力限制。项目选择双路 V4L2 是设计目标，实际节点可用性仍需 BSP/板端验证。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 92. 端到端延迟

- **考察：** 端到端延迟 的工作原理、边界和验证方法。
- **标准答案：** 端到端延迟应从 Sensor/驱动 timestamp 到显示或网络消费逐段测量：capture、pair wait、AI age、compose、MPP、ffmpeg/RTMP。当前只记录部分相对时间和统计字段，没有可引用的实测毫秒数。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 93. max_wait

- **考察：** max_wait 的工作原理、边界和验证方法。
- **标准答案：** `max_wait` 是 FramePairer 等待另一相机样本的上限，默认 50 ms；到期后 drop-older 或生成 stale hold-last。它控制延迟上界和配对成功率，不代表两 Sensor 同步。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 94. 掉帧定位

- **考察：** 掉帧定位 的工作原理、边界和验证方法。
- **标准答案：** 定位掉帧要按层拆分：V4L2 DQBUF/error、pairer timeout/source drop、AI queue drop、pair queue drop、encode error 和 sink error。只看最终 FPS 无法知道瓶颈；项目已有多类 counters，但未完成板端采集。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 95. 实时性验证

- **考察：** 实时性验证 的工作原理、边界和验证方法。
- **标准答案：** 实时性验证需要固定输入、帧率和运行时长，记录 FPS、P95/P99 latency、queue depth、CPU/RSS 和错误。当前性能表全为 NOT_MEASURED，synthetic 测试只能验证算法，不代表 RK3588 实时性能。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 96. 一路断开

- **考察：** 一路断开 的工作原理、边界和验证方法。
- **标准答案：** 一路断开时 capture loop 会累积 dequeue 错误，达到阈值后 `fail()` 停止 pipeline；pairer 会丢弃无法配对的样本。是否需要降级为单路输出是产品策略，当前代码没有宣称该能力。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 97. 磁盘写满

- **考察：** 磁盘写满 的工作原理、边界和验证方法。
- **标准答案：** 磁盘写满会让 raw `ofstream` 或 ffmpeg MP4 pipe 写失败；当前 raw sink 返回 `good()`，MP4 sink 记录 `mp4_errors` 并尝试重开一次。没有验证磁盘配额、错误传播和录像完整性。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 98. RTMP 断线

- **考察：** RTMP 断线 的工作原理、边界和验证方法。
- **标准答案：** RTMP 断线时 `FfmpegPipe::write()` 关闭并按原命令重开一次，再重试当前 packet；连续失败只返回 false 并计数。由于写入同步，断线期间仍可能阻塞编码线程，服务器重连未实测。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 99. 分步验收

- **考察：** 分步验收 的工作原理、边界和验证方法。
- **标准答案：** 分步验收应先验证 media graph/单路采集，再配帧、AI/OSD、单次 MPP、raw H264、MP4、RTMP，最后做异常和长稳。`docs/BOARD_ACCEPTANCE.md` 给出顺序；当前仅结构/Python检查通过。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

### 100. 最大工程风险

- **考察：** 最大工程风险 的工作原理、边界和验证方法。
- **标准答案：** 当前最大风险不是 API 数量，而是未闭环的外部契约：MPP completion、真实 V4L2 plane/timestamp、Sensor/rkAIQ/硬件同步、模型布局、ffmpeg 播放和性能数据。面试时应把这些列为验证缺口，并给出可执行验收步骤。
- **本项目例子：** docs/BOARD_ACCEPTANCE.md、docs/PERFORMANCE.md；对应实现只代表 SOURCE，CXX/BOARD 状态见上方标签。
- **深入追问：** 你能指出具体函数、错误路径和还缺什么硬件/性能证据吗？
- **常见错误：** 把设计意图、SDK API 存在或静态检查说成实机验证。
- **优先级：** P0/P1

## 练习方法

先遮住答案，按“结论→源码路径→验证状态→局限”口述 60～90 秒；遇到 THEORY_ONLY 要主动说是扩展学习。

