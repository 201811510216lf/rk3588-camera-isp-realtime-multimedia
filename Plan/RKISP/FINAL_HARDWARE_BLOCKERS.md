# 仍需SDK/BSP/硬件的项目

| 项目 | 当前证据 | 解除条件 |
|---|---|---|
| 官方BSP/kernel/RKISP30 | 未提供 | commit、驱动与明确平台能力 |
| Sensor及media路由 | 未提供真实设备/资料 | 原始media-ctl/v4l2-ctl与实测拓扑，不能使用testfixture |
| RKAIQ SDK API端口 | SDK_BACKEND_MISSING | 真实头库/示例，编写并核验RKAIQ_ADAPTER_SOURCE，编译/链接probe |
| SDK版本/ABI | UNKNOWN/未编译 | 版本查询/构建记录、匹配目标ELF及真实API调用检查 |
| IQ身份/版本/加载 | NOT_MEASURED | 指定实际文件与SDK/合法服务查询确认 |
| 外部服务逐Camera所有权/IPC | DISCOVERY_ONLY | 厂商所有权协议或合法客户端接口；进程名不足为证 |
| IN_PROCESS双ctx/组与启停顺序 | 软件phase/fake已测 | BSP指定start/stop顺序、资源锁/线程约束、部分失败实机回滚 |
| AE/AWB/曝光/gain生效 | 软件能力/回读契约已测 | SDK实际支持并readback，必要时图像/传感器证据 |
| 厂商myDemo构建 | 未参加本地主机构建 | RGA/MPP/RKNN/OpenCV/RKAIQ匹配工具链 |
| 输出/画质/24/72h | 未测量 | 双真实Camera、原始数据/日志、固定采集条件与合格阈值 |
| 自动Camera reconnect | NOT_IMPLEMENTED | 后续专门恢复状态机与实机验证 |

当前可完成的HAL/配置/发现/生命周期/就绪与诊断软件已实现并测试；没有RKAIQ真实SDK API、真实3A、硬件同步、DMA零拷贝、IQ标定或长稳完成声明。当前CMake拒绝未核验静态archive，若BSP仅提供静态库需补成员架构核验后再启用。
