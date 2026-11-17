# 阶段 8：运行管理

读取阶段 6/7 输出报告及 behavior/event_manager 实现。新增 runtime/monitor.h/.cpp，并在主 pipeline 中连接 Camera、AI、encoder、raw/recorder/network 心跳、错误和队列深度；按 stats_interval_sec 采样 health.jsonl。

Linux 采集 /proc/self/statm 的 RSS、进程 CPU 累计秒数、可用磁盘空间、可读时 thermal_zone0 温度/cpu0 当前频率。温度/频率不可读为 -1，磁盘状态带 disk_known。这些是采集能力，当前没有 RK3588 性能样本。

日志按大小重命名轮转，单行超限拒绝；二进制换行保证 Windows/Unix 相同字节预算。轮转仅处理自身日志，不触碰视频。事件及录像索引随实验保存，不自动删除；没有默认过期清理任务。

网络进程最多 5 次启动、指数退避、等待 IDR；通用 RetryBudget 提供有界重试软件组件。尚未实现 Camera 自动重开或 NPU 硬件恢复，独立模式隔离单路故障；AI 推理失败只记录错误，不阻塞录像。磁盘低于 --min-free-mb（默认1024）或录像吞吐不足时显式停采并返回错误，保留原始数据。普通文件系统 I/O 仍无硬实时超时保证。

关闭：停止生产→关闭/排空帧队列→join AI/编码→排空录像sink/终止网络→finalize→释放残留 lease→STREAMOFF/映射/设备关闭。启动失败由相同析构边界回收；异常关闭不把 partial.mp4 当作完整文件。

runtime_monitor_test 检查过期/独立心跳、错误、backoff 上限与延迟、磁盘状态、轮转及精确字节预算。24/72h真实运行、CPU负载曲线、散热与磁盘故障恢复仍为 BLOCKED_HARDWARE，检查计划见 HARDWARE_ADAPTATION_ROADMAP.md。
