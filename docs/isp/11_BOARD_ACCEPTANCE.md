# RK3588 板端验收清单

| 阶段 | 证据 | 当前状态 |
|---|---|---|
| E0 BSP/SDK | kernel、rkAIQ、IQ、交叉工具链版本 | NOT_TESTED |
| E1 I2C probe | dmesg、chip ID | BLOCKED_BY_HARDWARE |
| E2 sensor init | stream on/off、mode | BLOCKED_BY_HARDWARE |
| E3 media graph | media-ctl -p | NOT_TESTED |
| E4 RAW10 | 原始帧和 packing | NOT_TESTED |
| E5 RKISP NV12 | v4l2 capture | NOT_TESTED |
| E6 rkAIQ/IQ | context/load log | NOT_TESTED |
| E7 AE/AWB | 控制回读与统计 | NOT_TESTED |
| E8 单路稳定性 | 30 min counters | NOT_TESTED |
| E9 双路配帧 | pair delta/丢帧 | NOT_TESTED |
| E10 AI/拼接/MPP | 可解码 H.264 | NOT_TESTED |
| E11 RTMP/MP4 | 服务器和播放器证据 | NOT_TESTED |

每次记录实际命令、退出码、平台、日志路径和后续硬件动作。仓库已有 `scripts/isp_probe.sh`、`media_graph_validator`、`scripts/isp_capture_experiment.sh`。

## 面试要点

验收按 E0→E11 分层，任何一步失败都不跳步宣称端到端完成。
