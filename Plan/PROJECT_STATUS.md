# 当前交付状态（2026-11-17）

| 能力 | 状态 | 证据与剩余边界 |
|---|---|---|
| 帧/lease/NV12平面、有界队列、配帧 | IMPLEMENTED | 主机单元/集成/压力测试；特殊BSP垂直布局待适配 |
| V4L2双Camera/独立管理 | HARDWARE_UNVERIFIED | Linux源码；独立故障隔离、显式节点/格式/buffer配置，无Camera自动恢复 |
| 模型契约/CPU预处理/Tracking/时间事件 | IMPLEMENTED | 主机fixture；真实模型、精度、时序行为有效性未验证 |
| RKNN硬件 | PARTIAL + BLOCKED_HARDWARE | 复用特定YOLOv5 adapter，真实SDK/模型查询待验证；无通用时序模型runner |
| RGA/OSD/合成 | PARTIAL + BLOCKED_HARDWARE | 实际RGA调用保留；CPU fallback明确区分，行为状态OSD为UNAVAILABLE |
| MPP | PARTIAL + BLOCKED_HARDWARE | stride对齐与生命周期修订；显式PTS/flush/EOS/拆分输入输出线程待适配 |
| 独立sink、索引、离线关联、运行监控 | IMPLEMENTED软件层 | 有界队列/退避/日志/磁盘门槛测试通过；Linux子进程/真实视频需验证 |
| MP4/RTSP/RTMP | HARDWARE_UNVERIFIED | 保留FFmpeg，CFR时间轴；无实际服务器/ffprobe证据 |
| RkAiq应用控制、DMA-BUF完整路径 | INTERFACE_ONLY / BLOCKED_HARDWARE | BSP服务控制，未声称应用初始化/零拷贝 |
| Software ISP | 独立实验保留 | 算法测试和模拟smoke；不作为主产品输入 |
| 双真实Camera、模型部署、24/72h | BLOCKED_HARDWARE | 无设备和模型，不生成硬件性能或精度 |

源码重构并不等于最终硬件验收。当前机器为Windows；厂商路径未参加本地主机构建。根CMake各SDK选项验证依赖，完整myDemo还需要配套Linux/OpenCV和所有既有厂商adapter。模型/实验录像均未删除或上传。
