# 性能指标与证据范围

| 指标 | 当前采集/统计 | 当前实测结论 |
|---|---|---|
| Capture/AI/encode FPS | 每Camera计数、运行时平均 | 无RK3588样本 |
| 配帧P50/P95/P99/max | 最近4096匹配滚动分位数、累计max | 只做软件fixture正确性测试 |
| AI latency | 事件统计infer_ms | 无真实模型延时 |
| queue depth/highwater/drop | 帧队列/sink统计、health.jsonl | 多线程重复压力通过，非硬件吞吐 |
| CPU/RSS、温度/频率 | Linux proc/sysfs与累计CPU时间 | 无目标板曲线；不可读指标有状态/哨兵值 |
| 磁盘/分段索引 | space可用字节、finalize/源帧映射 | 主机软件契约验证；真实MP4待测 |
| 编码/事件吞吐 | 成功包/最终事件计数 | 无编码器/NPU/24h性能数据 |

本地主机18项CTest通过、4组各100次压力的实际时间见08_TEST_RESULTS。Standalone Software ISP smoke输出的simulated/measured_fps是PC无节流实验吞吐，不能推广为Camera或NPU FPS。未给出任何未经测量的实时帧率、模型准确率、零拷贝或24小时稳定性结论。

远程Linux软件测试与ASan/UBSan压力耗时已记录在08_TEST_RESULTS；它们仍不是RK3588性能样本。
