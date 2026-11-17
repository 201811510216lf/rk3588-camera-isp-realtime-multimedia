# RKISP / RkAiq 本轮执行状态

基线：GitHub main=7376c5a，开始时工作树干净；本轮以实际源码为准。该基线已经包含IRkAiqBackend、AiqController与RkAiqManager，本次复用并补强，不重复建立同名控制层。

| 验证层 | 当前结论 |
|---|---|
| 主机源码 | Backend/Manager、图映射、readiness、CLI增量已实现 |
| Windows/MSVC Debug完整构建 | 26/26CTest通过，3.17秒 |
| Windows/MSVC AddressSanitizer | 26/26CTest通过，24.44秒，无ASan报告 |
| Windows/MSVC压力 | 8组各100次，共800次通过，41.64秒 |
| Python CLI / SDK检测 / JSON诊断 | 实际可执行文件与负例测试均被完整CTest调用 |
| Linux / ASan+UBSan | 本轮推送后由GitHub CI核验；旧0017938结果不当作本轮证据 |
| 真实RKAIQ SDK | 未提供headers/libraries/核验API port，SDK_BACKEND_MISSING / BLOCKED_HARDWARE |
| 真实RK3588/Sensor/IQ/3A/24h | 未实测，BLOCKED_HARDWARE |

修复：失败的capability/readback不能保留旧控制确认；不确定生命周期必须清理，Manager关闭阶段不能再启动；每路query_all独立保留状态与错误；协调器拒绝重复/乱序阶段并将回调异常结构化；严格模式运行期丢失确认会停止，不只记录一条错误。

工具：isp_control_cli与isp_debug_tool共享src/isp/isp_cli.cpp。旧debug别名继续支持，数字错误不会未捕获异常退出；--list只列真实/dev字符设备，不将项目目录文件冒充Camera。--get-*明确查询AE/AWB/exposure/gain，缺SDK/IPC非零；--sdk-info输出版本/来源与availability，不从文件名猜SDK版本。

图：JSON保留每路Sensor/entity/pad/link/enabled_path/plane/stride/错误与来源。MIPI/CIF/ISP/Mainpath/Selfpath名称仅角色提示，绝不授予READY；Capture格式/FPS不再被Video Output段污染。

详见[本轮交付](../Plan/RKISP/INCREMENTAL_CONTROL_REPORT.md)、[项目状态](PROJECT_STATUS.md)及[板端手册](RK3588_RKISP_BRINGUP.md)。
