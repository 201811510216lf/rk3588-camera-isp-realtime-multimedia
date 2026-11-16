# 双路 IMX415 同步

## 同步语义

仓库 `FramePairer` 做内核时间戳近邻匹配，是软件配帧；它不证明同一时刻曝光。硬件同步必须有 Sensor FSIN/trigger、板级连线、驱动支持及示波器/帧开始证据。

## 当前实现

`lib/frame_pairer.cpp`、`lib/dual_camera_pipeline.cpp` 保留两路独立 capture、队列、AI context 和配对策略。`--sync-mode hardware` 在无真实触发后端时 fail-fast。双路 DPHY/I2C/供电未由仓库证明。

## 验收顺序

先独立验证 Camera0/1 的 I2C、media graph、RAW10/NV12 和帧率；再记录序列号/monotonic timestamp 配对误差；最后才评估 FSIN master/slave。若只有一路物理 CSI，保留软件架构并标记 BLOCKED_BY_HARDWARE。

## 状态

SOURCE_IMPLEMENTED：软件配帧；BOARD_VERIFIED：NOT_TESTED；HARDWARE_SYNC：NOT_IMPLEMENTED/BLOCKED_BY_HARDWARE。

## 面试要点

明确区分软件时间戳、同一时钟域和同步曝光，不能把“30 fps 双路”写成硬件同步。
