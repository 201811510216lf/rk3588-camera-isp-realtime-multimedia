# 阶段 7：独立输出与分段录像

读取阶段 6 报告后，保留 FFmpeg 协议栈，新增 output/AsyncPacketSink 和 FfmpegProcess。编码包为共享不可变 bytes，每个 raw、MP4、network sink 有独立工作线程和有界队列。编码线程只入队，不调用网络 I/O。

网络队列满时丢弃未发 GOP 并等待下一个含 SPS/PPS 的 IDR；单 sink 错误/异常不阻塞其他 sink。录像队列满或写入失败会显式停止采集，保留已有数据，不以成功状态隐藏丢失。暴露 accepted/consumed/dropped/errors/depth/high-water/failed 统计。

Linux FFmpeg adapter 使用 posix_spawnp + 独立 argv，不拼 shell 命令。父端 socket 为 nonblocking，poll/send(MSG_NOSIGNAL) 每包默认 1 秒期限；子进程关闭先 EOF 等待 1.5 秒，再 TERM/KILL 的有限等待。网络最多 5 次进程启动，带指数退避，恢复仅从 IDR 开始。RTMP/RTMPS 使用 FLV；RTSP 使用 TCP 发布，CLI `--publish-url` 兼容旧 `--rtmp-url`。真实服务器/FFmpeg 环境尚未验证。

MP4 按配置 segment_sec 在关键帧轮换单个 FFmpeg process，每个 muxer 仅由自己的 worker 写入。文件名包含 UTC wall-clock 纳秒、camera/joint 和分段编号。先写 *.partial.mp4；FFmpeg 成功退出并重命名后才输出 finalized:true，失败保留 partial 并向 pipeline 返回 finalize 错误。现有 raw 文件拒绝覆盖。每段开始和周期健康采样检查磁盘，阈值通过 --min-free-mb 配置，默认 1024；不自动删除实验数据。

每路/joint 的 index JSONL 保存真实接受包的 segment_id、video_offset_ns、有理 time_base/PTS 和每路源 camera/sequence/source_timestamp。结束另写 finalize 状态。行为事件与最终录像通过源序号和 finalize:true 分段关联；离线关联工具及其测试见阶段 8 报告。

时间边界：source nanoseconds 仅用于源关联；monotonic 用于期限/健康；wall-clock 仅用于命名。编码包 PTS 是 1/fps 的 CFR frame tick，FFmpeg 从 elementary H.264 重建容器时间轴，不保留原始采集间隔。实际 muxer DTS/PTS、丢帧时长影响、音画同步（本项目无音频）、故障恢复和异常文件可恢复性必须板端用 ffprobe 验证，不能声称 VFR 时间轴已完成。

主代理已报告 17/17 主机 CTest 通过，包括 output_pipeline_test：时间基换算/溢出、命名/索引、阻塞网络不阻塞独立 recorder、网络队列 GOP 恢复、录像溢出失败、回调异常和 100 次关闭循环。Linux 子进程、真实 RTSP/RTMP、MP4 文件和 SDK 路径是 `BLOCKED_HARDWARE`，未以 Windows 测试冒充通过。普通文件系统写入仍受操作系统/磁盘 I/O 期限约束，不能宣称所有磁盘故障下都有硬实时关闭上界。
