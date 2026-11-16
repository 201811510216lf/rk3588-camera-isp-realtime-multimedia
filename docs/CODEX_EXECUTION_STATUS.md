# E0-E6 执行状态

证据等级：`SOURCE_IMPLEMENTED`、`HOST_BUILT`、`HOST_TESTED`、`TARGET_BUILT`、`BOARD_TESTED`、`MEASURED` 分开记录。

| 阶段 | 源码 | 主机 | 目标/板端 | 记录 |
|---|---|---|---|---|
| E0 审计/基线 | `SOURCE_IMPLEMENTED` | 结构脚本可运行；CMake/C++ 编译器缺失 | `NOT_MEASURED` | 清理旧四路业务依赖，保留 V4L2/MPP/RGA/RKNN |
| E1 正确性 | `SOURCE_IMPLEMENTED` | `NOT_MEASURED`；无 C++ toolchain | `NOT_MEASURED` | lease、plane/offset、FramePairer、packet PTS 校验和错误传播 |
| E2 双路/配帧/拼接 | `SOURCE_IMPLEMENTED` | synthetic tests present, `NOT_MEASURED` build | `NOT_MEASURED` | 两路独立 capture、时间戳匹配、左右 layout |
| E3 BSP/ISP | `SOURCE_IMPLEMENTED`（只读 validator + fail-closed rkAIQ adapter） | Python 静态检查通过 | `AWAITING_BSP_EVIDENCE` | 未猜 Sensor、I2C、GPIO、DTS、IQ 或厂商 API |
| E4 AI/OSD | `SOURCE_IMPLEMENTED` | `NOT_MEASURED` | `NOT_MEASURED` | 两个 context、camera_id/timestamp 关联、过期结果不画；当前 RKNN wrapper 没有已确认的 core-mask API，参数会明确告警并使用 SDK 默认调度 |
| E5 RTMP/MP4 | `SOURCE_IMPLEMENTED`（单编码包 fan-out + ffmpeg pipes） | `NOT_MEASURED` | `AWAITING_SERVER_AND_BOARD` | 需真实 ffmpeg、服务器、ffprobe/播放器 |
| E6 测量/优化 | `SOURCE_IMPLEMENTED` 工具和统计字段 | `NOT_MEASURED` | `NOT_MEASURED` | 无 RK3588/BSP/设备，所有 FPS/同步/延迟填 NOT_MEASURED |

本次未执行的项目不是“通过”：CMake configure/build、CTest、Linux V4L2、MPP/RGA/RKNN、RTMP、MP4 播放、Sensor 硬件同步、长稳和真实曝光测量均需目标环境。

## 当前环境命令证据

- `python tests/validate_project.py` → exit 0，`PASS (32 required files)`。
- `python -m compileall -q tests scripts` → exit 0。
- `git diff --check` → exit 0。
- `python scripts/analyze_isp_samples.py` 对 synthetic 4×4 NV12 → exit 0，亮度统计正确，曝光/增益/时间戳明确为 `NOT_MEASURED`。
- `cmake -S . -B build-host ...` → exit 1：当前 PATH 没有 `cmake`；因此 C++/CTest 未宣称通过。

## 四项独立结论

- `SOURCE_IMPLEMENTATION`: `IMPLEMENTED`（双路用户态主链路、FramePairer、OSD/Compose、单 MPP 包分发、只读 ISP/rkAIQ 适配已落地）。
- `BUILD_TEST_STATUS`: `HOST_STATIC_TESTS_PASS / CXX_BUILD_NOT_EXECUTED`。
- `HARDWARE_SYNC_STATUS`: `NOT_VERIFIED`；`hardware` 模式 fail-fast。
- `END_TO_END_BOARD_STATUS`: `AWAITING_HARDWARE_AND_SERVER`。
