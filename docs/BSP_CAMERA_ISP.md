# RKISP30 / RkAiq 软件接入状态

当前软件实现已从发现工具演进为可插拔ISP控制层；真实SDK/Camera/板端状态仍BLOCKED_HARDWARE。任务源码入口为src/main.cpp，保留既有DualCameraPipeline与V4L2IspCamera。

- AiqController复用旧API并增加结构化backend、IQ/所有权验证、能力与控制回读、生命周期与双路管理。EXTERNAL_SERVICE只发现，不启动第二个context；IN_PROCESS依赖真实SDKport及明确BSP启停顺序；UNAVAILABLE不会调用SDK。
- media_graph_validator复用结构化parser/registry。必须提供真实Sensor/ISP/output配置并核对enabled路径、active NV12/NV12M、plane/stride/尺寸/FPS。没有默认media0或按节点序号推断身份。
- 默认strict ISP readiness。离线文件、Fake、候选服务或IQ目录不构成READY；仅明确allow_unverified诊断模式允许未验证采集。实际V4L2协商再次绑定配置。
- CLI发现/能力/IQ状态与JSON转储可在无SDK时运行；AE/AWB/曝光/gain写入明确非零失败。设置接受不等于硬件确认。独立CLI不接管IN_PROCESS会话，真正控制需要所属running上下文或合法IPC。
- isp-health.jsonl记录服务观测、真实采集心跳、首帧/后续stall、FPS/sequence gap、状态和错误；capture worker不做AIQ初始化。恢复状态NOT_IMPLEMENTED。
- probe只读；capture脚本为明确请求的有界采集；sample_aiq_status与离线分析保留来源、不覆盖既有证据，不填虚构3A/质量/收敛结果。

本轮Windows普通与AddressSanitizer各25/25CTest通过；实际SDK编译和双真实Camera未验证。构建条件、真实结果及剩余项见[测试报告](../Plan/RKISP/07_TEST_REPORT.md)、[SDK矩阵](../Plan/RKISP/07_BUILD_MATRIX.md)、[最终交付](../Plan/RKISP/FINAL_RKISP_DELIVERY_REPORT.md)。

板端15项验收与带占位符命令见[RK3588 RKISP Bringup](RK3588_RKISP_BRINGUP.md)。新配置示例[isp_config.example.yaml](../configs/isp/isp_config.example.yaml)仅结构说明；本轮没有确认Sensor型号或IQ文件，旧IMX415实验配置不代表当前硬件身份。
