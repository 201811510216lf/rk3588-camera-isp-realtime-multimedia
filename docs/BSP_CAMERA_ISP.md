# RKISP30 / RkAiq 软件接入状态

当前软件实现已从发现工具演进为可插拔ISP控制层；真实SDK/Camera/板端状态仍BLOCKED_HARDWARE。任务源码入口为src/main.cpp，保留既有DualCameraPipeline与V4L2IspCamera。

- AiqController复用旧API并增加结构化backend、IQ/所有权验证、能力与控制回读、生命周期与双路管理。EXTERNAL_SERVICE只发现，不启动第二个context；IN_PROCESS依赖真实SDKport及明确BSP启停顺序；UNAVAILABLE不会调用SDK。
- media_graph_validator复用结构化parser/registry。必须提供真实Sensor/ISP/output配置并核对enabled路径、active NV12/NV12M、plane/stride/尺寸/FPS。没有默认media0或按节点序号推断身份。
- 默认strict ISP readiness。离线文件、Fake、候选服务或IQ目录不构成READY；仅明确allow_unverified诊断模式允许未验证采集。实际V4L2协商再次绑定配置。
- CLI发现/能力/IQ状态与JSON转储可在无SDK时运行；AE/AWB/曝光/gain写入明确非零失败。设置接受不等于硬件确认。独立CLI不接管IN_PROCESS会话，真正控制需要所属running上下文或合法IPC。
- isp-health.jsonl记录服务观测、真实采集心跳、首帧/后续stall、FPS/sequence gap、状态和错误；capture worker不做AIQ初始化。恢复状态NOT_IMPLEMENTED。
- probe只读；capture脚本为明确请求的有界采集；sample_aiq_status与离线分析保留来源、不覆盖既有证据，不填虚构3A/质量/收敛结果。

当前增量Windows普通与AddressSanitizer各26/26CTest通过，8组各100次压力测试通过。代码a436b5f的Linux普通与ASan+UBSan也各26/26及800次压力通过；真实SDK编译和双真实Camera未验证。本轮实际结果见[执行状态](CODEX_EXECUTION_STATUS.md)与[增量报告](../Plan/RKISP/INCREMENTAL_CONTROL_REPORT.md)。原25组测试及SDK接入约束保留在[历史测试报告](../Plan/RKISP/07_TEST_REPORT.md)、[SDK矩阵](../Plan/RKISP/07_BUILD_MATRIX.md)。

板端15项验收与带占位符命令见[RK3588 RKISP Bringup](RK3588_RKISP_BRINGUP.md)。新配置示例[isp_config.example.yaml](../configs/isp/isp_config.example.yaml)仅结构说明；本轮没有确认Sensor型号或IQ文件，旧IMX415实验配置不代表当前硬件身份。

## 本次控制层增量

`isp_control_cli`与`isp_debug_tool`共用同一dispatcher。debug的`--status`、`--iq-info`、`--ae-status`、`--awb-status`、`--camera`、`--set-exposure`、`--set-gain`、`--list`别名保留。以下只读命令不初始化Camera：

```bash
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --sdk-info --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --get-controls --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --get-ae-mode --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --get-exposure-us --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --get-gain --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --camera-id 0 --get-awb-mode --json
./build-host/isp_control_cli --isp-config /data/isp.yaml --media-graph --json
./build-host/media_graph_validator --config /data/isp.yaml --json
```

无真实backend或合法IPC时，控制查询/设置返回非零，不能从默认值伪造AE/AWB或曝光。`--sdk-info`给出sdk_available、real_hardware_backend、sdk_version及version_source；无SDK时版本UNKNOWN、来源NOT_MEASURED。`--list-devices`（debug别名`--list`）只列真实Linux `/dev`字符设备，DISCOVERY_ONLY不建立Camera映射。

图JSON包含Sensor实体、entity/pad/link、启用路径、配置ISP实体与NV12/NV12M平面/stride/FPS。MIPI/CIF/ISP/Mainpath/Selfpath角色标明来自报告function或名称提示；名称提示不授权映射或READY。离线图只验证结构，`hardware_confirmed=false`且`isp_3a_ready=NOT_MEASURED`。Capture格式和FPS不再从Video Output段取值。

控制层失败回读会清除旧确认；不确定生命周期进入需清理状态。Manager在部分关闭或关闭失败后禁止启动剩余phase，每路`query_all`分别保留状态/错误。协调器先完成所有Camera发现，再创建backend；回调异常变为结构化错误。运行期Error、严格模式失去硬件确认会停止采集；显式诊断仍保留未验证/降级标记。AE/AWB、IQ匹配、实际服务所有权和BSP启动顺序必须在目标SDK/开发板验收。
