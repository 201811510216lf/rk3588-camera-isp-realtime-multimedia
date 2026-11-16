# 2026-11-16 代码差异与文档一致性审查

审查基线：`1d0a5d6` → `ab50e37` 的架构演进与本轮工作区修正。审查以源码、CMake 目标、实际入口和已获得的命令输出为准；旧计划和旧审查不直接当作当前验收结论。用户后续要求以代码差异为主，不再运行测试；本轮最终修正没有构建/测试复验。

## 已确认并修正

| 问题 | 源码/差异依据 | 本轮修正 |
|---|---|---|
| 旧单路计划仍像当前强制架构，部分审查引用已删除业务与旧行号 | `main.cpp` 已改为 `DualCameraPipeline`；`myDemo` 显式源清单不含 `mipi_pipeline.cpp` | README 重写两条独立入口；增加 CURRENT_ARCHITECTURE；历史计划/审查加范围说明 |
| `--npu-core0/1` 被接收到配置，却在创建模型时都传固定 0；错误告警还声称 wrapper 无核 API | `dual_camera_pipeline.cpp` 的模型构造与 `rknnPool.hpp` 中已有 `rknn_set_core_mask` 冲突 | 两个 context 各自传入配置；-1 映射 AUTO，0/1/2 显式选择；CLI 和 wrapper 拒绝其他范围；删除错误告警 |
| Gamma 控制测试拿相同配置比较 | `IspConfig::gamma` 默认 2.2；原 `test_controls_change_output` 第二次仍设 2.2，因此两个输出相同 | 将第一条线性基线显式设为 1.0；保留差异断言，不更改实际默认 gamma 或弱化断言；未复验 |
| CTest Python 发现规则只匹配一个文件，漏掉另一组已有检查 | `test_isp_quality_analyzer.py` 与 `isp_quality_analyzer_test.py` 均已存在 | 模式统一为 `*isp_quality_analyzer*.py`；本轮未重新执行 |
| 结构校验仍强制旧 MipiPipeline 与旧单路计划验收标记 | `tests/validate_project.py` 必需清单与当前独立 Software ISP 构建不一致 | 不再要求旧单路源文件/计划状态字符串；补当前架构和软件模块路径；依然仅是结构检查 |
| 同步 packet 分发被写成 sink I/O 隔离 | `EncodedPacketDistributor::publish` 同步顺序调用回调 | 修正文档为慢 sink 可回压 Encode，不能承诺断网不影响录像或故障下有界退出 |
| Software ISP 首帧样例、辅助曲线和硬件能力混写 | demo 仅 n==0 导出图像/拼接；报告脚本使用独立合成图和预设曲线 | 明确首帧输出、元数据配帧统计、主机吞吐、模拟 AI 与真实 RKISP/rkAIQ/RKNN/MPP 的边界 |
| “PATH 无编译器”被延续为当前机器无编译器 | 本轮找到 Visual Studio 2022 自带 CMake/MSVC | 记录基线 Debug 主机编译成功、首轮 CTest 有失败；最终修正未复验，不宣称全绿 |

## 仍需后续工作的代码边界

1. `FfmpegPipe` 的阻塞 `fwrite`、`pclose` 和一次重新打开不提供有界关闭。重连路径未保证重新发送 SPS/PPS 并从 IDR 恢复；不能承诺流连续或录像完全隔离。需要目标服务器/BSP 环境下设计异步 sink、超时和编码流恢复，未在本轮无目标证据时扩展重构。
2. `camera_isp_demo` 的配帧对象仅供统计，当前首帧拼接直接使用当轮两张图像，尚未绑定实际返回的 FramePair；它也不输出完整视频或生产 OSD。部分实验文件写入未完整传播 I/O 错误，不能以进程退出 0 单独证明文件有效。
3. 软件 AI 后端的 ONNX/RKNN 枚举是接口占位；真实后端未接入。生产 RKNN wrapper 仍依赖既有模型输出/后处理约定，需实际模型元数据核实；未根据输入格式名称推断并修改 SDK 可转换的布局行为。
4. 真实 Sensor/CSI/RKISP/rkAIQ、硬件同步、DMA-BUF import、MPP completion、RGA fence/cache、RKNN 调度与长稳都没有目标验证。本轮 Windows 主机构建不编译 `myDemo`，不能据此声称目标路径可构建。
5. Python 结构校验和合成报告不是 C++ 功能验证，历史单元测试数量也不能当作本轮最终提交的通过证明。

## 实际验证范围

已有命令输出见 [CODEX_EXECUTION_STATUS.md](CODEX_EXECUTION_STATUS.md)：基线 Python 结构检查和 9 个 Python 检查通过，MSVC Debug 主机编译成功，首轮 CTest 10/11 项通过（Gamma 测试比较相同配置）。用户明确“不用运行测试”后未再启动测试或构建，以上修正以差异审查交付。

本轮没有改变远端地址或分支，不删除历史报告，不合并含旧敏感祖先的本地备份历史，不进行强制推送。
