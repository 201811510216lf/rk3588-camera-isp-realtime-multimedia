# V4L2 Buffer 生命周期

摄像头池拥有 fd、mmap 地址和可选 EXPBUF fd。CameraFrame.owner 和 FrameLease.owner 共享池的生命周期；消费者只借用 payload，不能 munmap、close 或自行 QBUF。

1. 配置 NV12/NV12M，REQBUFS/QUERYBUF 后分别映射每个存储平面。
2. QBUF 使 slot 进入 Queued；DQBUF 只能取回 Queued slot，继而进入 Dequeued。
3. 校验 plane_count、data_offset、bytesused、映射长度、协商 stride；无效 payload 直接回收或停止错误流，不暴露地址。
4. acquire 成功后 slot 进入 InUse。Frame 多个消费者复制共享同一 FrameLease，不会生成独立回收责任。
5. 最后一个消费者释放 lease，回调执行唯一 QBUF，再减少 in_flight。stop_accepting 后的释放不再 QBUF。
6. shutdown：停止生产者并 join → close 业务队列 → 消费者 drain/join → 清理 Pairer/保留帧 → stop_accepting / 等待 in_flight=0 → STREAMOFF → munmap → close。

超时未归还的 lease 保留共享池和映射，stop 报错，不能为退出而提前 unmap。驱动错误可 STREAMOFF 停止生产，但已有 lease 仍保持映射直到释放。返回 QBUF 不代表下游 RGA/MPP 异步硬件已经完成；适配异步调用时必须把完成信号纳入 lease 的最终释放条件。

CameraBufferDesc.timestamp_ns 是驱动源时间，source_timestamp_ns() 提供同值只读访问；monotonic_timestamp_ns 是本机 DQBUF 后的观测时间，不能声称是曝光时间。plane_count 是存储平面数：NV12=1（UV 在 offset_uv），NV12M=2（独立 data_uv）。bytesused 表示 data_offset 后实际 payload，mapped_length 是完整 mmap 长度。DMA fd 默认 -1，未验证 import/coherency。

状态：软件所有权和内存布局可在主机测试；真实 RK3588 V4L2、ISP、DMA-BUF 均为 `BLOCKED_HARDWARE`。
