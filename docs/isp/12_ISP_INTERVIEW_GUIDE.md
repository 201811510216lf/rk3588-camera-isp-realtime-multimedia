# ISP 岗位面试要点

## 必答主题

IMX415 mode table 与 lane/link frequency；Sensor subdev controls；DTS endpoint 双向连接；RKCIF/RKISP media graph；rkAIQ context/IQ/3A；RAW10/12 packing；NV12 stride/plane；软件配帧和硬件同步的差异；RKNN/RGA/MPP buffer ownership。

## 结合本仓库的真实表述

可以说明已实现用户态 V4L2 NV12/NV12M、lease 生命周期、FramePairer、双路 AI/OSD、RGA/MPP 分支和只读 ISP 探测；不能声称已经完成 IMX415 kernel 驱动、rkAIQ AE/AWB、IQ 加载、双路硬件同步或板端 RTMP。

## 证据路径

`src/v4l2_isp_camera.cpp`、`src/frame_pairer.cpp`、`src/aiq_controller.cpp`、`src/media_graph_validator.cpp`、`scripts/isp_quality_analyzer.py`、`docs/FINAL_ACCEPTANCE_REVIEW.md`。

## 状态

SOURCE_IMPLEMENTED：用户态骨架；BOARD_VERIFIED：NOT_TESTED；所有硬件结论以 `13_ISP_IMPLEMENTATION_STATUS.md` 为准。
