# 阶段6：ISP诊断与长时观测

已读取阶段5双路集成报告。复用现有诊断脚本，新增结构化状态采样和 `IspHealthMonitor`，不改变产品输入源、ISP算法或V4L2采集职责。

## 运行观测

`include/isp/isp_health_monitor.h` / `src/isp/isp_health_monitor.cpp` 为两路Camera提供线程安全且有界的观测窗口。实际DQBUF提供原始sequence、源时间戳和本机monotonic时间；窗口输出arrival FPS、已知源时钟下的source FPS、帧间隔P95、sequence gap/重复/回退、时间戳异常、错误计数和stall。来源保留 `LIVE_DEVICE` / `OFFLINE_FILE` / `MOCK_OR_TEST` / `NOT_MEASURED`，Fake测试不会变为硬件证据。

`capture_started` / `capture_stopped` 明确采集期待状态。STREAMON成功后即开始首帧期限，零DQBUF也能报告stall；未启动或已停止的Camera不误报采集stall。重启清空速率窗口及跨会话比较基线，累计计数保留。服务观测与采集心跳分别过期，配置/所有权错误优先于“仍有帧到达”。Camera自动重连没有实现，状态固定 `NOT_IMPLEMENTED`。

服务观测是周期backend查询及只读进程发现，不是假定存在厂商IPC心跳；`service_observation_ns` 是检查时间。Sensor身份、IQ加载、AE/AWB、曝光/增益的确认均来自backend结果。周期 `isp-health.jsonl` 保留完整状态快照；候选进程名和采集FPS不能证明3A已就绪。具体pipeline调用与BSP启停顺序见阶段5。

## 状态采样脚本

`aiq_sample_capture.sh` 复用 `isp_control_cli --isp-status --json`，委托 `sample_aiq_status.py` 处理JSON/CSV转义、独占创建输出及每次CLI超时：

```bash
bash scripts/aiq_sample_capture.sh --cli ./build/isp_control_cli \
  --isp-config /data/isp.yaml --camera-id 0 --samples 60 \
  --interval-sec 1 --timeout-sec 5 --output /data/observations.csv
```

记录wall-clock与monotonic的**观测时间**，不命名为capture/exposure时间。CLI非零、超时、错误JSON或错误Camera响应均写入失败记录，整个采样返回非零，不能静默变成成功。无确认readback时AE/AWB/曝光/增益为 `NOT_MEASURED`；即使测试来源声称confirmed也不会提升为硬件readback。brightness始终未测量，必须从图像另行计算。不会覆盖已存在的CSV。

## 离线图像与DQBUF元数据

`analyze_isp_samples.py` 现在按帧流式读取实际Y行，支持显式stride、storage height、UV offset和每帧存储大小；NV12M必须显式提供独立UV文件及plane大小，不猜测某工具输出的多平面拼接布局。完整文件/plane数量必须一致，短帧拒绝。显式payload大小允许省略最后有效行之后的padding。默认packed布局仅是调用者选择，不能替代板端协商证据。

输出256-bin luma histogram、mean/stddev、可配置阈值clipping ratio、横向相邻差值的结构敏感noise proxy和逐帧平均亮度变化。结果标 `OFFLINE_FILE_ANALYSIS` 且 `hardware_confirmed=false`；不推断曝光/增益、AE收敛、Sensor噪声标定或专业IQ评价。每帧摘要默认最多128条，统计窗口有界，整个长文件不一次载入内存。

可选CSV使用整数纳秒相减，避免epoch时间转float损失；按camera_id隔离，检查零/重复/回退时间、时钟域变化和sequence gap。跨时钟域清空间隔窗口，未知或异常时钟不报告已确认FPS；缺少sequence时标明不完整，不把缺失元数据当作丢帧。P50/P95为最近4096个有效间隔窗口，仍只是所提供元数据的离线分析。

`isp_probe.sh` 保持只读，并标明systemd/process/runtime都是所有权线索，不是逐Camera归属证明。`isp_capture_experiment.sh` 是显式请求的抓帧操作，只有同时给出宽高时设置NV12格式；它与只读probe分开。采用唯一证据目录、查询/抓帧TERM后KILL期限、失败保留partial文件，记录真实协商输出；不会伪造DQBUF时间或IQ加载状态。普通内核/存储阻塞仍不能宣称硬实时关闭保证。

## 真实验证与边界

- `python -m unittest discover -s tests -p test_isp_diagnostics.py -v`：实际10/10通过，覆盖padding/多帧、NV12M、bytesused末行、时间精度/时钟/sequence、CSV转义与保护、CLI失败/超时和Fake来源隔离。
- Git Bash对三个修改的shell脚本执行 `bash -n`：3/3通过；抓帧脚本无参数/缺一维/负帧数三种负例均退出2，没有设备或采集动作。
- Python `py_compile` 通过。父任务阶段5完整构建25/25通过；最后增加的首帧stall API与生命周期/SDK guard修订统一等待阶段7完整重建，不能沿用旧结果冒充最新通过。
- 真实RK3588、双Camera、RkAiq/AE/AWB readback、真实亮度序列、24/72h、BSP service heartbeat及自动恢复验收均为 `BLOCKED_HARDWARE` 或明确未实现。本阶段没有生成硬件性能、画质或收敛数据。
