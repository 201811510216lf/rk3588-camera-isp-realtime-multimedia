# 阶段 3：双路 V4L2 和统一帧

读取 `00_REPOSITORY_AUDIT.md`、`01_BUILD_AND_LAYOUT.md` 后复用 CameraFrame、V4L2IspCamera 和 DualCameraPipeline，避免创建第二套图像结构或重复 CameraManager。DualCameraPipeline 承担双 Camera 管理职责；两个 V4L2 对象各有采集线程、AI 有界队列和统计。

本阶段修改：
- CameraBufferDesc 补 camera_id、source_timestamp_ns()（timestamp_ns 的只读访问器）、本机 monotonic_timestamp_ns 和实际 mmap 长度；保留已有 stride、offset、bytesused、sequence、DMA fd、共享 lease。
- `src/camera_frame.cpp` 校验 NV12 单存储平面、NV12M 双存储平面、有效载荷和 stride；打包复制只遍历有效像素，不能把 width*height*3/2 当驱动分配大小。
- V4L2 在读取 mplane plane_fmt 前检查 num_planes；QUERYBUF/DQBUF 平面数必须与协商格式相等；防止重复 QBUF；请求帧率后再回读实际值。
- 未显式声明 MONOTONIC 的 V4L2 timestamp 保持 Unknown，不能假设 realtime 域；本机 dequeue 观测时间与源时间分开保存。
- 取消 /dev/video0、/dev/video1 默认值。CLI 要求两个显式、不同的设备节点；支持 `--capture-buffers 2..64`、`--pixel-format auto|NV12|NV12M`。`--help` 无需硬件配置。
- BufferLeaseState 拒绝同一 slot 重复 acquire 和重复 release，最终共享 lease 释放后只回收一次。

单元覆盖：camera_queue_test（带 padding 的单/双平面、截短 payload、无效 offset/stride、重复 lease 释放），既有 camera_lifecycle_test。构建结果统一由阶段 9 `08_TEST_RESULTS.md` 记录；不将接口和内存测试记为硬件采集验证。

板端状态：`BLOCKED_HARDWARE`。真实 QUERYCAP/ENUM_FMT/S_FMT/REQBUFS/QUERYBUF/MMAP/QBUF/STREAMON/DQBUF/STREAMOFF 路径已在源码中，当前主机没有 Linux RKISP 节点；RK3588 BSP、两个真实 Sensor、实际 stride/height alignment 和 DMA coherency 均待验证。已有 EXPBUF 尝试只证明存在导出代码，不证明全链路 DMA-BUF import 或零拷贝。

验证更新（主代理）：Windows Ninja/MSVC Debug clean-first 58 项构建动作完成，13/13 CTest 通过，包含 camera_queue_test。真实 V4L2 硬件未验证。
