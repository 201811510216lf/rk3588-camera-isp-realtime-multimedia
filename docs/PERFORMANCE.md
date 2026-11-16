# 性能与测量记录

| 指标 | 当前状态 | 来源/条件 |
|---|---|---|
| Cam0 FPS | `NOT_MEASURED` | 需要板端 V4L2 DQBUF 统计 |
| Cam1 FPS | `NOT_MEASURED` | 需要板端 V4L2 DQBUF 统计 |
| Pair FPS | `NOT_MEASURED` | `FramePairer` `matched_pairs / elapsed` |
| abs(delta) P50/P95/P99 | `NOT_MEASURED` | 同一 clock domain 的原始 V4L2 timestamp |
| AI0/AI1 FPS 与推理 P95 | `NOT_MEASURED` | 两个 RKNN context，记录 camera_id 与 result age |
| Encode FPS/端到端延迟 | `NOT_MEASURED` | 单 MPP encoder，目标板实测 |
| CPU/RSS/in-flight buffer | `NOT_MEASURED` | 30/60 分钟稳定性测试 |
| RTMP 重连/MP4 分段 | `NOT_MEASURED` | 真实服务器、ffprobe/播放器 |

主机可执行测试中的 synthetic timestamps、队列拥塞、closest-candidate、坐标映射、packet fan-out 和媒体图失败路径只证明软件算法和错误传播，不代表 RK3588 性能或硬件通过。板端采集建议每 5 秒输出：capture/pair/encode/AI FPS、queue depth、source drop、pair drop、AI result age、P50/P95/P99、MPP/RGA/RKNN error 和 RSS。

RTMP/MP4 sink 当前把同一 Annex-B 包流写入 ffmpeg stdin，并提供协商 FPS；ffmpeg 管道没有携带应用内部的逐包 PTS，精确 PTS/DTS、断线重连和服务器时钟行为必须用真实板端/服务器验证后再记为 `MEASURED`。
