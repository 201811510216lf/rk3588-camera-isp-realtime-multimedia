# 当前执行状态与证据

核对日期：2026-11-16（Asia/Shanghai）；审查基线为 `ab50e37`，本轮最终变更见 Git 提交差异。当前架构以 [CURRENT_ARCHITECTURE.md](CURRENT_ARCHITECTURE.md) 和 CMake 目标清单为准。

## 已执行的证据

以下命令在用户后续提出“不用运行测试”之前执行。此后没有继续构建或测试；修正后的最终快照未复验，不宣称全绿。

| 检查 | 实际结果 | 适用范围 |
|---|---|---|
| `python tests/validate_project.py` | PASS，基线检查 36 个所需文件 | 文件/关键字结构检查，不证明编译或行为正确 |
| `python -m unittest discover -s tests -p '*isp_quality_analyzer*.py'` | 9 项通过 | Python RAW/质量分析工具 |
| CMake configure + MSVC Debug build | 成功 | `BUILD_RK_DEMO=OFF`，软件库、demo、BSP 工具和主机测试可编译 |
| `ctest --test-dir build-review-msvc -C Debug --output-on-failure` | 10/11 项通过；`software_isp_test` 失败 | `linear.data != gamma.data` 断言，原 `tests/software_isp_test.cpp:69`；该测试两次都使用默认 gamma=2.2，本轮将线性基线设为 1.0，保留差异断言；未复验 |
| 本轮最终变更 | 代码差异审查；未再构建/测试 | 按用户要求保留未复验状态 |
| 真实 Linux/RK3588、厂商 SDK、Sensor/模型/服务器 | `NOT_VERIFIED` | 未执行目标构建、硬件同步、MPP/RGA/RKNN、rkAIQ、端到端推流和长稳 |

环境发现：CMake/CTest/MSVC 已随 Visual Studio 2022 安装，但不在原 PATH。使用安装目录的 CMake、Visual Studio 17 2022 x64 generator、Debug 配置构建到忽略目录 `build-review-msvc`，保留 assert。原生 Windows 不编译 `myDemo`。已登记的 WSL Ubuntu 因 VHD 路径不存在而未能启动，没有 Linux 构建证据。

## 能力边界

| 能力 | 源码状态 | 验证边界 |
|---|---|---|
| 双路 V4L2 → FramePairer → Compose → 单 MPP 编码 | 已接当前 `main` 和 `myDemo` 清单 | 未目标构建/板测 |
| 目标 RKNN、RGA、RTMP/MP4 | 有实现路径 | 缺实际 SDK/模型/服务器运行证据；同步 sink 存在阻塞风险 |
| 硬件曝光同步 | 未实现触发后端 | `hardware` 模式 fail-fast；时间戳配帧不能证明同时曝光 |
| NPU 核参数 | 修正为每路传入配置，-1 使用 SDK AUTO、0/1/2 显式绑核 | 仅源码审查，未目标构建/板测 |
| 真实 rkAIQ/IQ/硬件 AE/AWB | 未接入，fail-closed | 与 Software ISP 的软件 AE/AWB 分开 |
| VirtualSensor/RAW10/Software ISP/AE/profile/Demo AI | 主机实现，基线 Debug 编译成功 | 首轮 CTest 有一项失败；最终修正未复验 |
| 双路软件 demo | 串行虚拟处理、元数据配帧统计、首帧拼接样例 | 未实现配对帧驱动的完整逐帧视频输出 |
| `reports/isp` 的辅助图像/JSON | Python 合成样例 | 不能证明 C++ AE/AWB 收敛、帧率或板端成像质量 |

本轮发现、修正和剩余问题见 [代码差异审查](CODE_RECONCILIATION_2026-11-16.md)。历史报告中的“无编译器”“C++ 未执行”、固定测试数量和旧 Git 状态仅描述当时环境。
