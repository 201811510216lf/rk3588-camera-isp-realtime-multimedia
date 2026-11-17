# 官方 RK3588 BSP 适配与24/72h验收

1. 固定官方BSP/kernel、架构工具链、RKISP30驱动、RkAiq/IQ、RGA/MPP/RKNN版本及model hash。确认已有源码再发布授权和SDK许可，保留原始声明。
2. 用media-ctl/v4l2-ctl核对Sensor→CSI→RKCIF→RKISP拓扑、两个真实输出节点、NV12/NV12M格式/stride/bytesused/plane及帧率。BSP/IQ由板级服务初始化。确认特殊storage height与UV offset，不能从sizeimage猜。
3. 在Linux构建ENABLE_V4L2；完整myDemo使用真实SDK头库，运行双路DQBUF/QBUF/故障关闭及慢消费者lease测试。验证单路失败独立录像继续；joint本来受配帧约束。
4. 使用真实RGA SDK对照颜色矩阵/量化范围/resize/crop/compose和完成语义；CPU参考不能替代硬件结果。DMA-BUF import/export与fence另行实现和验证。
5. MPP补齐真实SDK的显式PTS/DTS、输入输出worker、flush/EOS和错误恢复；确认16对齐I420和buffer完成边界。必要时替换为实际支持的NV12输入，不能在未验证前声称零拷贝。
6. RKNN sidecar与导出模型匹配，查询tensor rank/layout/type/scale/输出schema；与CPU参考输入逐像素/张量比对，验证anchors/类别和后处理。普通检测不能替代时序行为识别；另测遮挡ID切换、标签误报、事件持续时间。
7. FFmpeg/ffprobe实测首个IDR/SPSPPS、分段GOP、容器PTS/DTS/time_base、正常与异常finalize、partial恢复、索引逐帧可定位。CFR保留采集间隔问题若实验要求VFR须新增适配，当前未实现。
8. 网络中断/服务器不响应/重连耗尽、AI慢/崩溃、单Camera断开、磁盘不足/写失败、进程信号逐项验收；录像损失必须可见。普通存储I/O无硬实时保证，默认不自动清理原始实验数据。

24h先单路基线再双路independent和joint：每秒/5秒采样capture/encode/AI FPS、latency、queue highwater/drop、配帧P50/P95/P99、CPU/RSS、温度/频率、磁盘及错误。每小时抽查MP4和事件索引。合格阈值须按真实分辨率/帧率/模型/存储预算预先确定，不能使用PC吞吐值。

72h在24h通过后进行：同时安排断网/恢复、低磁盘门槛、单路故障、NPU负载、日志轮转、重复启动/信号停止；检查无持续RSS增长、无lease泄漏、无无界重启、原始数据保留。归档BSP日志、版本、配置、原始监控曲线、ffprobe结果与失败记录。以上均为待执行计划，状态BLOCKED_HARDWARE。
