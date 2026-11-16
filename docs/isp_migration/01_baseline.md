# G0：基线与环境限制

日期：2026-11-12

初始源码基线包含两套业务：旧 FFmpeg/MPP Decode/四模型/四宫格/RTMP 链路，以及新增但未完成验证的 MIPI 链路。旧链路的文件和构建引用已在本次收敛中删除；本文件只记录收敛前事实，不表示旧业务仍受支持。

## 本次可复现命令

| 命令 | 结果 |
| --- | --- |
| `python tests/validate_project.py` | PASS (19 required files) |
| `git diff --check` | PASS；仅有 Git 的 LF/CRLF 提示 |
| `cmake -S . -B build-host ...` | 未执行：当前 PATH 没有 CMake |
| `cmake --build ...` / `ctest ...` | 未执行：没有 CMake、g++、clang++ 或 cl.exe |
| Linux V4L2/MPP/RGA/RKNN 构建 | 未执行：没有 Linux 头文件、厂商 SDK 或 RK3588 设备 |

仓库内历史 `build/myDemo` 来自另一台 Linux 主机，不能作为当前源码构建或运行证据。

## 已知边界

没有 RK3588、MIPI Sensor、Media Controller、rkAIQ/IQ 文件和目标 BSP，无法验证 Sensor→CSI→ISP→V4L2、实际 plane layout/fps、MPP 超时完成点、RGA 同步、RKNN 性能、输出码流可解码性或长时间稳定性。所有这些事项在 `docs/ARCHITECTURE_REFACTOR_REPORT.md` 标为待上板。
