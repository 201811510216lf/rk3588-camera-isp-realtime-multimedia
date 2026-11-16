# 双 Sensor 硬件曝光同步可行性

当前结论：`HARDWARE_SYNC_STATUS=UNKNOWN_UNTIL_SENSOR_AND_BOARD_EVIDENCE`。

本环境没有两颗 Sensor 的数据手册、模组原理图、板级 DTS 或 RK3588 BSP checkout，不能安全断言存在 FSIN/Trigger、主从 frame-sync 寄存器、共享时钟或可用 GPIO。软件 `FramePairer` 的时间戳匹配只能形成同一合成帧，不能证明两颗 Sensor 同时曝光。

## 必须核对的真实证据

对每颗 Sensor 记录以下原始资料和版本：

- 数据手册中 FSIN/trigger/master-slave 引脚、电平、脉宽、最小 setup/hold、rolling-shutter 限制。
- 模组和开发板原理图中 FSIN 是否实际连到同一源、GPIO/PWM/外部时钟的电压域和方向。
- BSP Sensor subdev driver 是否实现 frame-sync control、stream-on 顺序、frame-length/exposure 限制和错误回滚。
- DTS/DTSI endpoint、pinctrl、clock/reset/pwdn 和外部触发属性；每一项都必须对应真实文件行。
- 板端示波器/逻辑分析仪或 Sensor SOF metadata 证据，确认两个帧起点的相位和长期漂移。

## 软件行为

- 默认 `software`：`FramePairer` 在同一时钟域且时间戳定义一致时按 `abs(t0-t1) <= tolerance` 选择单调最小差值，并统计丢帧、超时和 P50/P95/P99。它不会把启动顺序、同名 FPS 或 NPU/VPU 并行当作曝光同步。
- `hardware`：在没有真实板级 trigger backend 和验证证据时必须 fail-fast。不得通过“开启某个 GPIO”或虚构寄存器把模式标成成功。
- `hold-last`：仅是展示降级，必须标记 `STALE/UNSYNCED`，不计入真实配对成功；严格同步验收使用 `drop-older`。

## 板端验收顺序

1. 先分别单路抓取并保存 `DQBUF sequence/timestamp/timestamp_flags`，确认时间戳触发点是 SOF、EOF 或其他定义。
2. 启用双路软件配帧，保存 pair CSV，计算 `abs(delta_ns)` 分布和两路 source drop；不得只记录线程 `clock_gettime()`。
3. 若驱动和电路均支持外部触发，再按 Sensor 手册配置共同帧率/曝光范围，测量 SOF 相位和 rolling-shutter 行时间；失败时回退软件模式。
4. 将所有硬件触发命令、DTS commit、示波器截图/日志和回滚步骤归档到 `docs/isp_migration/hardware/`。

在这些证据完成前，报告应使用：`S1 software pairing: SOURCE_IMPLEMENTED`、`S2 kernel timestamp: AWAITING_BOARD`、`S3 hardware exposure sync: NOT_VERIFIED`。

