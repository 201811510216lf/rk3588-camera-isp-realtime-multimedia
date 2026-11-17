# 功能/API差距
| 功能 | 初始状态 | 改造方向 |
|---|---|---|
| V4L2采集/lease | IMPLEMENTED / BLOCKED_HARDWARE | 保留 |
| 图发现 | PARTIAL | 结构化pad/link/route与capture当前格式 |
| Sensor到output映射 | MISSING | 配置+拓扑证据 |
| Aiq状态发现 | DISCOVERY_ONLY | 区分服务、runtime、IQ与来源 |
| 真实RKAIQ系统/AE/AWB API | SDK_BACKEND_MISSING | 只有实际SDK声明/库/适配包才允许链接 |
| 生命周期/双路管理 | MISSING | 可注入backend、顺序与失败回滚 |
| readiness gating | MISSING | 默认严格、显式诊断降级 |
| 质量/长稳指标 | PARTIAL | 离线/设备/fake来源分离，缺值NOT_MEASURED |
| fake backend | MOCK_OR_TEST | 仅测试目标，不注册产品模式 |
