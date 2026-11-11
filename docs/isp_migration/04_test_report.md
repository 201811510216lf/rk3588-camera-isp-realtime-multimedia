# G6：构建、测试和硬件状态

日期：2026-11-11

## 本机可执行验证

已执行的依赖无关检查：

```text
python tests/validate_project.py
validate_project: PASS (19 required files)
```

同一输出已保存到 `docs/isp_migration/validate_project.log`。

VS Code C/C++ 扩展自带的 `clang-tidy --allow-no-checks` 已对 `lib/app_options.cpp`、`lib/v4l2_isp_camera.cpp`（非 Linux fallback）和 `tests/camera_lifecycle_test.cpp` 做 C++17 语法检查，三个命令退出码均为 0。它不是完整链接构建；Linux/板端仍需执行下面的 CMake/CTest 命令。

同一工具在强制 `-D__linux__` 时因当前 Windows 没有 `linux/videodev2.h` 退出 1；这正是板端 V4L2 SDK/内核头需要补齐的外部环境项。

PowerShell 中调用 Git for Windows 的 `bash -n scripts/isp_probe.sh` 受到当前沙箱的 `NtCreateDirectoryObject ... 0xC0000022` 限制，因此脚本语法检查需在 Linux/板端执行。

本次源码新增了无依赖的 `tests/camera_lifecycle_test.cpp`，覆盖：

- 两个消费者共享一个 lease 时只在最后释放后执行一次 QBUF；
- stop_accepting 后释放 lease 不再 QBUF；
- AI 有界队列容量和丢旧帧统计；
- `--source`、尺寸和非法选项解析。

预期执行命令：

```bash
cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

当前 Windows 执行环境没有 `cmake`、`ctest`、`g++`、`clang++` 或 `cl.exe`，WSL 没有可用发行版，因此上述命令在本机尚未执行，不能写成通过。板端或 Linux CI 应首先运行这些命令；`-DREQUIRE_RK_DEMO=ON` 可在依赖缺失时让配置明确失败。

仓库内旧的 `build/myDemo` 在 Windows 上尝试 `--help` 返回“拒绝访问”，因为它是另一台 Linux 主机生成的 ELF；没有用它替代当前源码构建。

## 板端测试矩阵

| 测试 | 当前状态 | 通过证据 |
| --- | --- | --- |
| `--help` / 非法参数 | 代码和 host mock 已覆盖，未在本机编译运行 | `isp_host_tests` 输出 PASS；CLI 返回非零 |
| mipi 不存在设备 | 代码路径已实现，未在本机编译运行 | `--source=mipi --device=/dev/does-not-exist` 返回具体 open errno，资源释放 |
| V4L2 单/多平面 NV12 | 代码已实现，待板端 | `scripts/isp_probe.sh` + negotiated format/plane/stride 日志 |
| Buffer 双消费者/关闭 | host mock 源码已覆盖，未编译运行 | 无重复 QBUF、无提前归还、退出无死锁 |
| AI 故意卡顿 | `--simulate-ai-ms` 已实现，待板端 | encode 继续、`ai_drop_oldest` 增长 |
| Camera→MPP→H.264 | 待 RK3588 + Sensor + MPP | raw H.264 可被解码器读取，记录 capture/encode FPS |
| ISP/rkAIQ/AE | 待对应 BSP/IQ | 保存实际 media graph、控制回读和 dmesg |
| 30 分钟/1 小时稳定性 | 待板端 | 保存 RSS、CPU、帧率、错误和丢帧统计 |

## 诚实状态

`IMPLEMENTED_AWAITING_HARDWARE`。软件适配点、错误处理、mock 测试和板端探测文档已实现；当前环境没有 RK3588、MIPI Sensor、rkAIQ 或厂商库，不能标记 `HARDWARE_VERIFIED`，也没有虚构性能指标。
