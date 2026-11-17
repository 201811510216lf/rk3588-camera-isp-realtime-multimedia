# 当前交付状态（2026-11-17）

| 能力 | 状态 | 证据与剩余边界 |
|---|---|---|
| 帧/lease/NV12平面、有界队列、配帧 | IMPLEMENTED | 主机单元/集成/压力测试；特殊BSP垂直布局待适配 |
| V4L2双Camera/独立管理 | HARDWARE_UNVERIFIED | 上一轮Linux CI已编译V4L2源码；独立故障隔离、显式节点/格式/buffer配置，无Camera自动恢复 |
| 模型契约/CPU预处理/Tracking/时间事件 | IMPLEMENTED | 主机fixture；真实模型、精度、时序行为有效性未验证 |
| RKNN硬件 | PARTIAL + BLOCKED_HARDWARE | 复用特定YOLOv5 adapter，真实SDK/模型查询待验证；无通用时序模型runner |
| RGA/OSD/合成 | PARTIAL + BLOCKED_HARDWARE | 实际RGA调用保留；CPU fallback明确区分，行为状态OSD为UNAVAILABLE |
| MPP | PARTIAL + BLOCKED_HARDWARE | stride对齐与生命周期修订；显式PTS/flush/EOS/拆分输入输出线程待适配 |
| 独立sink、索引、离线关联、运行监控 | IMPLEMENTED软件层 | 有界队列/退避/日志/磁盘门槛测试通过；Linux子进程/真实视频需验证 |
| MP4/RTSP/RTMP | HARDWARE_UNVERIFIED | 保留FFmpeg，CFR时间轴；无实际服务器/ffprobe证据 |
| RkAiq软件HAL、DMA-BUF完整路径 | IMPLEMENTED软件 / BLOCKED_HARDWARE | 生命周期/就绪/控制契约已测；真实SDK API与DMA路径未验证 |
| Software ISP | 独立实验保留 | 算法测试和模拟smoke；不作为主产品输入 |
| 双真实Camera、模型部署、24/72h | BLOCKED_HARDWARE | 无设备和模型，不生成硬件性能或精度 |

源码重构并不等于最终硬件验收。当前机器为Windows；厂商路径未参加本地主机构建。根CMake各SDK选项验证依赖，完整myDemo还需要配套Linux/OpenCV和所有既有厂商adapter。模型/实验录像均未删除或上传。

历史提交5d92bef的Linux普通及ASan+UBSan CI均18/18通过，各400次压力通过，见08_TEST_RESULTS；这不是本轮ISP代码的Linux结果。只证明主机Linux软件和V4L2代码可编译；真实Camera及厂商myDemo仍未验收。

## RKISP专项后续更新

原先RkAiq“仅接口”边界已扩展为完整软件控制框架、显式所有权/IQ/生命周期/双路回滚、readiness gating和ISP健康层。默认外部模式只发现，IN_PROCESS仍需真实SDK API port，未声称硬件3A已接通。

本轮Windows/Ninja完整25/25、MSVC ASan25/25、ISP四组各100次压力通过；旧LinuxCI不是本轮结果。真实目标SDK/myDemo编译、RKISP30/3A、Sensor/IQ与24/72h仍BLOCKED_HARDWARE，详见[专项交付](RKISP/FINAL_RKISP_DELIVERY_REPORT.md)。

上传后新增验证：0017938的GitHub Linux普通与ASan+UBSan均25/25通过，各800次压力通过，见RKISP/07_TEST_REPORT.md。真实SDK与硬件状态不变。

## 当前main增量

基于7376c5a再次补强既有控制层：生命周期故障锁定、双路phase与query_all、readiness异常/运行期失效、Sensor拓扑JSON、共用CLI查询与SDK负例检测。Windows/MSVC与ASan各26/26通过，8组各100次压力通过。当前增量状态见[docs/PROJECT_STATUS](../docs/PROJECT_STATUS.md)、[执行证据](../docs/CODEX_EXECUTION_STATUS.md)与[增量报告](RKISP/INCREMENTAL_CONTROL_REPORT.md)。真实SDK和板端仍BLOCKED_HARDWARE。
