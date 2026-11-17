# 阶段 4：有界队列和双路帧调度

读取 `02_V4L2_DUAL_CAMERA.md` 后保留 FramePairer，扩展已有 BoundedFrameQueue。

队列支持 push/tryPush、pop/tryPop/waitPop、毫秒超时、close 唤醒、capacity/size、high-water mark、accepted push/pop/drop/closed rejection/timeout 计数。BLOCK 等待空间；DROP_OLDEST 替换最旧；DROP_NEWEST 拒绝新值；LATEST_ONLY 始终只保留最新值。close 后拒绝新输入，但消费者可排空已接受值；阻塞生产者和消费者都会唤醒。旧 push_drop_oldest/pop 入口保持兼容。容量为零不会接受数据。

FramePairer 保留 tolerance/max_wait、drop-older、显式 stale hold-last、最大队列深度、camera/sequence/时钟域检查。新增：
- P50/P95/P99 样本限制为最近 4096 次匹配，可由 max_stats_samples 调整；累计 max_delta/matched/drop 仍保留全程计数，不再无限保存 24 小时样本。
- 收到样本计数及 matched_frame_ratio=2*matched_pairs/(received0+received1)。stale 不算真实匹配。
- 新帧入队前过期已有样本，迟到另一视角不能复活超时帧。
- 重复 sequence/timestamp 作为乱序丢弃；hold-last 不跨不兼容时钟域复用；config() 取得锁后按值返回。

双视角 joint 模式使用 FramePairer；independent 模式由主 pipeline 每路独立录像队列和编码器处理（具体 SDK 验证边界见阶段 6/7），不等待另一 Camera 配帧。AI queue 和录像分支互相独立。软件时间戳接近不等于 Sensor 曝光同步，未提供硬件触发后端。

新增 camera_queue_test 覆盖：四种策略、timeout、满队列关闭唤醒、空队列关闭唤醒、8 轮 start/drain/close、4 生产者共 64000 次阻塞队列传递、NV12/NV12M 布局、最后 lease 归还、滚动统计、迟到超时及 hold-last 时钟隔离。既有 frame_pairer_test 覆盖正常、漂移、丢帧、乱序、深度上限和最近候选。

真实运行结果见阶段 9 `08_TEST_RESULTS.md`；当前 SDK/板端仍为 `BLOCKED_HARDWARE`。统计分位数描述滚动软件窗口，不是板端长期同步精度。

验证更新（主代理）：Windows Ninja/MSVC Debug clean-first 58 项构建动作完成，13/13 CTest 通过，包含 camera_queue_test。真实 V4L2 硬件未验证。
