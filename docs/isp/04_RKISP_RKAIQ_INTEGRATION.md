# RKISP / rkAIQ 集成

## 处理链路

RAW Bayer 经 black level、坏点/镜头阴影、demosaic、AWB/CCM、gamma、降噪、锐化和格式转换后输出 NV12。实际模块顺序和可用参数以 RK3588 BSP ISP 文档、驱动和 rkAIQ 版本为准。

## 当前实现

`lib/v4l2_isp_camera.cpp` 实现 NV12/NV12M V4L2 capture、plane/stride/bytesused 校验和 lease 生命周期；`lib/aiq_controller.cpp` 只做运行时发现和状态导出，所有 AE/AWB/曝光/增益写操作在未识别 BSP API 时返回 `Unsupported`，没有伪造 rkAIQ 符号。

## 真实 rkAIQ 接入要求

锁定 SDK commit、头文件和 IQ schema 后，再实现独立 `RockchipAiqBackend`：每路独立 context、IQ 匹配、AE/AWB/AF（若硬件支持）、3A 统计、参数提交、exposure/gain feedback、start/stop 和错误回滚。不能用另一版本的示例函数名替代。

## 调试命令

```bash
isp_debug_tool --status --camera 0
isp_debug_tool --iq-info --camera 0 --iq-root /etc/iqfiles
isp_debug_tool --ae-status --camera 0
```

`tools/isp_debug_tool.cpp` 是面向验收命令名的只读/fail-closed 工具；历史 `isp_control_cli` 保留兼容参数。`--set-exposure`、`--set-gain`、`--ae-status`、`--awb-status` 在未识别真实 BSP backend 时返回退出码 3（`Unsupported`），不会假写寄存器。

## 状态

SOURCE_IMPLEMENTED：fail-closed 状态接口；BOARD_VERIFIED/SDK_VERIFIED：NOT_TESTED；BLOCKED_BY_HARDWARE：缺 rkAIQ headers、runtime、IQ 和板端 context。

## 面试要点

把“ISP 输出 NV12”与“rkAIQ 已初始化并闭环控制曝光”分开举证；双路必须拥有两个 context 和独立统计状态。
