# 构建矩阵

| 路径 | 实际状态 |
|---|---|
| Windows/Ninja/Debug，ENABLE_RKAIQ=OFF | clean build+25/25通过 |
| Windows/Ninja/Debug，ENABLE_ASAN=ON | 编译及25/25运行通过（工具链本地DLL加入生成目录） |
| ENABLE_RKAIQ=ON，无真实header/lib | 明确配置失败，负例测试通过 |
| 错误ELF e_machine/class、未知架构或未检查静态archive | 明确拒绝，未链接错误架构 |
| 有头库、无核验adapter source | 明确失败，不把存在文件当API已接入 |
| 真实Linux/aarch64 RKAIQ SDK及myDemo | BLOCKED_HARDWARE，未提供SDK |
| 本轮Linux普通及ASan+UBSan | 0017938的GitHub CI各25/25通过，各800次压力通过 |
| TSan | NOT_RUN，不将ASan/UBSan结论当作TSan结果 |

RkAiq配置项：ENABLE_RKAIQ（默认OFF）、RKAIQ_ROOT（空）、RKAIQ_INCLUDE_DIR、RKAIQ_LIBRARY_DIR、RKAIQ_SYSTEM_HEADER、RKAIQ_LIBRARIES（真实绝对库路径列表）、RKAIQ_EXTRA_INCLUDE_DIRS、RKAIQ_ADAPTER_SOURCE。无固定相对目录猜测RKAIQ。

当前ON路径只接受目标匹配的64位ELF共享库；静态archive未逐成员核验，因此拒绝。匹配Linux工具链、真实系统头和核验的adapter同时存在时，编译/链接rkaiq_sdk_link_probe（不自动运行初始化）。SDK adapter需实现项目C++工厂isp::create_sdk_backend，IRkAiqBackend不是Rockchip ABI，厂商函数只能依据该SDK头/示例编写。

Windows：powershell -File scripts/build_host_windows.ps1；ASan独立目录：powershell -File scripts/build_host_windows.ps1 -BuildDir build-rkisp-asan -Asan。Linux：bash scripts/run_tests.sh；GCC/Clang可独立配置ENABLE_ASAN_UBSAN或ENABLE_TSAN，当前没有执行结果。
