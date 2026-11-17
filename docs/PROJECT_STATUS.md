# 当前项目状态

本轮基于main=7376c5a增量修改，保留DualCameraPipeline、V4L2IspCamera、FramePairer及独立Software ISP。

| 功能 | 状态 | 限制 |
|---|---|---|
| IRkAiqBackend / AiqController | IMPLEMENTED软件 | 真实SDK适配没有提供，实际3A未测 |
| 两种所有权模式 | IMPLEMENTED软件 | 外部服务只读发现；无合法IPC时控制UNSUPPORTED，未知owner拒绝in-process初始化 |
| 生命周期/双路Manager | IMPLEMENTED软件+Fake测试 | 失败锁定、分阶段回滚/停止、独立query_all，SDK/BSP顺序待板端证据 |
| AE/AWB/exposure/gain | 能力/参数/回读软件已实现 | CLI未拥有上下文不擅自初始化；真实设置效果未确认 |
| SDK版本状态 | 版本/来源/availability可查询 | 无真实SDK为UNKNOWN/NOT_MEASURED，不能当检测到某版本 |
| MediaGraph / Mainpath / Selfpath | 结构化映射与角色提示已实现 | 名称提示非权威，Sensor实体/启用路径+NV12布局才是映射证据 |
| Pipeline ISP启动/运行/停止 | 已增量接入与host policy测试 | 厂商myDemo未在本机SDK编译，Capture线程不做AIQ初始化 |
| 主机完整测试 / ASan | 各26/26通过 | Fake/离线不等于实机 |
| 真实SDK编译 / 板端 / 24h | BLOCKED_HARDWARE | 缺BSP/SDK/Sensor/IQ/开发板 |

本次没有实现未知SDK函数、IspServer IPC或camera-group API。没有硬编码设备编号、改动DTS/I2C/media link、重启服务或用Fake替代真实运行。更多证据见[执行状态](CODEX_EXECUTION_STATUS.md)。历史视频/AI与ISP交付报告保留在[Plan项目记录](../Plan/PROJECT_STATUS.md)。
