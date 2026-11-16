# G6：构建、测试和硬件状态

日期：2026-11-12（Asia/Shanghai）

## 本次实际执行

| 命令 | 结果 |
| --- | --- |
| `python tests/validate_project.py` | PASS (20 required files) |
| `git diff --check` | PASS |
| `cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON` | 未执行：当前环境没有 CMake |
| `cmake --build build-host` / `ctest --test-dir build-host` | 未执行：没有 CMake/C++ 编译器 |
| `bash -n scripts/isp_probe.sh` | 未执行成功：Windows 沙箱拒绝启动 bash 子进程；需 Linux/板端执行 |
| RK3588 V4L2/MPP/RGA/RKNN、ASAN/UBSAN、AI 慢消费者、长稳 | 未执行：缺少板端、SDK、模型和编译环境 |

结构检查不是编译或硬件验收。历史 `build/myDemo` 来自另一台 Linux 主机，没有用于本次结论。

## 已覆盖的源码测试点

`tests/camera_lifecycle_test.cpp` 覆盖最后一个 lease 才 QBUF、停止后不再 QBUF、bounded queue 丢旧和单路 MIPI CLI 错误；真实执行需在有 CMake/C++ 编译器的环境运行。

## 待板端测试矩阵

| 测试 | 状态 |
| --- | --- |
| `--help` / 非法参数 | host 编译后执行 |
| 不存在的 V4L2 设备 | Linux host 或板端执行，核对 errno 和清理 |
| 单/多平面 NV12、非零 offset、bytesused、stride | 目标驱动 mock/板端 |
| MPP timeout/硬件错误与 pending buffer 回收 | 目标 BSP API 和压力注入 |
| Capture/Encode/AI 三线程背压 | 板端注入慢磁盘、慢编码、`--simulate-ai-ms` |
| Camera→MPP→H.264 可解码 | RK3588 + ISP NV12 节点 |
| Sensor/CSI/ISP/rkAIQ/AE/AWB | 对应 BSP、Sensor、IQ 文件 |
| 30/60 分钟稳定性、RSS、CPU、P95 延迟 | 板端采集统计 |

当前交付状态：`IMPLEMENTED_AWAITING_HARDWARE`。
