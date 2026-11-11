# G0：修改前基线

日期：2026-11-11

## 已执行命令和结果

| 命令 | 结果 |
| --- | --- |
| `pwd; git status --short; git branch --show-current; git log -1 --oneline` | 当前工作区不是 Git checkout（未发现 `.git`），四条 Git 查询均返回 `fatal: not a git repository`。没有执行破坏性 Git 操作。 |
| `rg --files`、阅读 `CMakeLists.txt`、`main.cpp`、`include/`、`lib/` | 完成；真实模块关系见 `00_repository_audit.md`。 |
| `cmake -S . -B build-baseline -DCMAKE_BUILD_TYPE=Debug` | Windows PowerShell 报 `cmake is not recognized`，未生成构建。 |
| `cmake --build build` | 无法执行，因为当前环境没有 `cmake`。已有 `build/` 的 CMakeCache 指向另一台机器的 `/home/firefly/项目/project1/...`。 |
| 当前快照的 `build/myDemo` | 文件是 Linux ELF（约 995 KB），来自旧路径，不能证明当前源码可构建或可运行。 |
| `build/myDemo --help`（Windows） | 启动被系统拒绝（Linux ELF），没有把旧二进制当作回归结果。 |

## 依赖缺口

当前快照没有 `../rknn`、`../mpp`、`../rga`、`../ffmpeg-lite` 和 `../opencv4-1` 目录；也没有 RK3588、MIPI RAW Sensor、V4L2 节点或 rkAIQ/IQ 文件。因此基线不能宣称旧 RTSP/MP4 运行通过，也不能进行板端 ISP 验收。

## 基线风险记录

源码审计还发现原入口缺少 `argc` 校验，旧四宫格线程无停止条件，模型是裸 `new` 且初始化失败直接退出，`StreamLoaderManager::unload_stream` 依赖短暂 sleep。新补丁只修复与退出、输入选择和可检查初始化直接相关的部分；legacy 的 FFmpeg/MPP/RGA/RKNN 业务路径保留。
