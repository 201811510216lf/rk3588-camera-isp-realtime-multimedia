# 阶段5：增量接入双路Pipeline

读取阶段2/3/4报告，保留DualCameraPipeline、V4L2IspCamera、FramePairer与全部视频/AI线程。新增IspReadinessProvider/IspPipelineCoordinator依赖，启动前配置→MediaGraph/逻辑映射→所有权/IQ验证→BSP指定AIQ阶段→V4L2启流→BSP指定after-stream阶段→最终就绪校验。Capture线程不做AIQ初始化。

默认strict_isp_ready=true。--isp-config加载显式双路映射，节点/宽高/FPS与应用参数必须一致；NV12/NV12M固定为每路配置，驱动协商后的尺寸/已知FPS/格式再次核对。观测到的映射、所有权或IQ不匹配不能通过诊断开关绕过。没有真实backend确认Sensor/IQ加载/SDK兼容、LIVE_DEVICE来源与RKISP30证据时，不默认READY。

只有显式--allow-unverified-isp（或配置strict=false/allow=true）允许诊断采集，始终标记UNVERIFIED_ISP。离线图、Fake、目录存在和候选服务名不会变成硬件ready。进程内模式必须显式start_order及stop_order；停止所有工作线程并释放lease后，按before-streamoff→V4L2 STREAMOFF→after-streamoff停止所属AIQ，外部服务不init/stop。

初始化错误及早期返回统一进入清理guard。任一已拥有AIQ的Camera启流失败，整组回滚以免启动未STREAMON的after-stream context；外部服务诊断模式保留独立录像单路故障隔离。SDK在after-stream启动失败时延迟清理到BSP指定停止阶段。

IspHealthMonitor连接实际DQBUF和周期状态查询，记录source/arrival FPS、sequence gap、错误、服务观测及stall；运行期观测为isp-health.jsonl，不把采集心跳等同于3A加载证据。Camera自动重连仍NOT_IMPLEMENTED。

真实验证：Windows阶段5clean build，25/25CTest通过（2.35秒），含配置/拓扑、生命周期fake、readiness与capture绑定、CLI退出码、诊断和SDK负例。最后针对stoporder/首帧stall/架构检测的修订会在阶段7再次完整构建。主机构建不包含厂商依赖myDemo；其接线已审查，实际SDK/双摄/BSP启停和3A仍BLOCKED_HARDWARE。
