# 板端验收清单

以下命令中的节点、模型、服务器和 IQ 路径必须由实际 BSP 证据替换；示例值不是已验证事实。

1. **BSP/Media Graph**：运行 `scripts/isp_probe.sh`、`media-ctl -p -d /dev/media0`、`v4l2-ctl --list-devices`，保存原始日志。用 `media_graph_validator` 确认两个独立 ISP NV12/NV12M 输出。
2. **节点基线与双路启动**：先用 BSP 工具分别检查两个节点；再以 `--camera0 ... --camera1 ... --no-ai --no-stream --duration-sec 30` 运行双路入口，确认 sequence、timestamp flags、stride、bytesused、QBUF/STREAMOFF。`myDemo` 没有单 Camera 运行模式。
3. **软件同步**：启用 `--sync-mode software --sync-policy drop-older --sync-diagnostics ...`，记录 pair count、drop、P50/P95/P99；不能把结果写成同时曝光。
4. **AI/OSD**：使用同一模型文件和两个 context，分别检查 camera_id、源 timestamp、AI result age、框是否出现在正确左右画面；`strict` 模式需记录超时丢帧。
5. **编码/录像**：检查单路 MPP 编码一次、SPS/PPS、单调 PTS/DTS；运行 `ffprobe` 检查分段 MP4，使用 `ffplay` 或实际播放器检查左右画面、框、类别、置信度和同步状态。
6. **RTMP**：连接真实服务器，记录重连、断网期间录像是否继续、服务端拉流 URL 和客户端截图/日志。
7. **异常**：拔掉一路、停止 ffmpeg、填满磁盘、注入 MPP timeout、Ctrl+C、反复启动退出；确认无死锁、lease 泄漏、未回收线程或错误 index。
8. **硬件同步**：只有拿到 Sensor 数据手册、DTS/driver diff、FSIN/Trigger 电平和 SOF/示波器证据后，才执行 `--sync-mode hardware`。否则预期结果是明确 fail-fast。

当前环境结果：上述所有 BOARD_TESTED、TARGET_BUILT、RTMP_SERVER_VERIFIED、MP4_PLAYER_VERIFIED 均为 `NOT_MEASURED`；主机本轮已执行的构建/检查及失败项见 [执行状态](../Plan/08_TEST_RESULTS.md)；这些证据不代表板端通过。
