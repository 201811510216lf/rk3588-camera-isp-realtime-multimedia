# RKISP软件控制层增量交付

## 基线与范围

本轮基于实际main=7376c5a，初始clean，原25组CTest基线全部通过。已有后端/管理器与图/就绪框架均复用；没有重写DualCameraPipeline、V4L2IspCamera、FramePairer，没有增加产品输入源。

## 实际修改

1. Backend优先：能力/回读失败传播并清除陈旧确认，有限范围与模式回读校验；部分生命周期失败锁定状态；运行期owner/Sensor失配返回明确错误。SDK版本仍由目标backend提供，缺SDK为UNKNOWN，新增JSON availability/version-source区分。
2. 映射：Sensor/MIPI/CIF/ISP/Mainpath/Selfpath/VideoNode角色提示及来源；配置的启用路径仍是权威映射。结构化JSON包含每路图、pad/link、路径、输出平面/stride/FPS与错误，离线不会成为READY。修复Output段污染Capture格式/FPS。
3. Manager：拒绝重复/非法启动阶段、部分关闭/失败关闭后禁止启动剩余阶段；query_all为每路保留状态和结果，独立反映AE/AWB回读失败。
4. Pipeline：阶段顺序/幂等清理、异常转结构化错误、全部发现完成后再建context、来源一致性与严格运行期确认。Error或严格就绪丢失停止采集，明确诊断模式仍标未验证/降级；ISP操作不放Capture线程。
5. 工具与构建：复用同一CLI dispatcher，debug旧别名和只读设备清单保留；新增get-controls/get-ae-mode/get-exposure-us/get-gain/get-awb-mode、sdk-info和media-graph JSON。多控制/参数错误非零且JSON不依赖--json位置。CMake额外拒绝目录假装头/适配源及缺库，未猜SDK API。

## 真实验证

Windows/MSVC Debug clean build：26/26CTest通过（3.17秒）。MSVC ASan独立目录clean build：26/26通过（24.44秒），无ASan报告。8组压力各重复100次，共800次通过（41.64秒）。包含扩展Fake生命周期/双路/失败回读、readiness异常/运行期故障、映射/输出作用域、CLI10项、Media Graph CLI3项及SDK8项检测。新JSON CLI测试直接调用已构建exe，不把fixture当运行Camera。

修改C++统一clang-format并dry-run核验。上传后Linux结果会在完成后追加；不把旧CI或未执行检查写作本轮成功。源码/SDK/实机证据分别列在docs/CODEX_EXECUTION_STATUS.md与docs/PROJECT_STATUS.md。

## 未完成的硬件部分

当前RKAIQ_ROOT/INCLUDE_DIR/LIBRARIES/ADAPTER_SOURCE均未提供，无实际BSP头/库/示例或Sensor/IQ。真实Backend仍SDK_BACKEND_MISSING/BLOCKED_HARDWARE；所有rk_aiq_*签名必须从目标SDK核验后编写port。ExternalService没有合法IPC则控制UNSUPPORTED；InProcess不允许抢占IspServer。真实RKISP30、SDK/myDemo编译、控制生效、Camera group及24h/72h均未验收。

文件清单见INCREMENTAL_CHANGED_FILES.txt。板端步骤见../../docs/RK3588_RKISP_BRINGUP.md。本轮完成后按用户要求以普通提交推送GitHub main，不覆盖远端历史。
