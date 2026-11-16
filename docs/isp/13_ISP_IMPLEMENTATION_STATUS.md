# ISP 实施状态与真实性验收

| 能力 | 状态 | 证据 |
|---|---|---|
| IMX415 驱动 | BLOCKED_BY_HARDWARE / NOT_IMPLEMENTED | 无目标 kernel、mode table、寄存器资料 |
| DTS/CSI/DPHY | BLOCKED_BY_HARDWARE | 仅有占位 overlay 和只读 validator |
| RAW10 采集 | NOT_TESTED | 无板端 RAW 文件 |
| RKISP NV12 | SOURCE_IMPLEMENTED（用户态消费）/ BOARD_VERIFIED NOT_TESTED | `v4l2_isp_camera.cpp` |
| rkAIQ 初始化 | NOT_IMPLEMENTED | `AiqController` fail-closed |
| IQ 加载 | NOT_TESTED | 无厂商 IQ/SDK |
| AE/AWB 控制 | UNSUPPORTED | 未识别真实 API，拒绝伪造 |
| RAW 质量分析 | SOURCE_IMPLEMENTED | `scripts/isp_quality_analyzer.py` |
| 双路软件配帧 | SOURCE_IMPLEMENTED | `FramePairer`/`DualCameraPipeline` |
| 双路硬件同步 | BLOCKED_BY_HARDWARE | 无 FSIN/trigger 线路证据 |
| RKNN/RGA/MPP 链路 | SOURCE_IMPLEMENTED / TARGET NOT_TESTED | 现有用户态模块 |
| RTMP/MP4 | TARGET NOT_TESTED | 需板端 ffmpeg/服务器/播放器 |

## 主机验证

`python tests/validate_project.py`、`python -m unittest discover -s tests -p 'test_isp_quality_analyzer.py'`、`python -m unittest discover -s tests -p 'isp_quality_analyzer_test.py'` 和 `python -m compileall -q scripts tests` 应在当前环境执行；CMake/CTest 若工具不可用必须记录 NOT_TESTED。此文件不把源码存在写成板端通过。

## 结论

当前交付是“可审查的 IMX415/RK3588 适配准备 + 用户态双路视频链路”，不是已完成的硬件 ISP 适配。拿到板卡/BSP/IQ 后按 `11_BOARD_ACCEPTANCE.md` 补证据，再把对应行升级为 BUILD_VERIFIED、SOFTWARE_TESTED 或 BOARD_VERIFIED。

## 面试要点

准确说清 SOURCE_IMPLEMENTED、HOST_TESTED、BOARD_VERIFIED、BLOCKED_BY_HARDWARE 的边界，比夸大端到端完成更可信。
