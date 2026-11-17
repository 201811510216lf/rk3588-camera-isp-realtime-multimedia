# IMX415 / RK3588 硬件兼容性审计

## 结论

本仓库没有绑定的 RK3588 开发板型号、原理图、BSP checkout、IMX415 模组实物或板端日志，因此本报告只给出**可追溯的兼容性前提**，不把 SoC 能力推导成板级能力。单路 IMX415 是可行的候选路径；双路只有在板级 CSI/DPHY、I2C、时钟和供电证据齐全后才可实施。

## 资料与证据

- Sony IMX415 产品资料：3840×2160、CSI-2 2/4 lane、RAW10/RAW12；寄存器和 mode table 必须以模组供应商交付的完整 datasheet/初始化表为准。
- Firefly CAM-8MS1M 页面列出 RK3588 兼容板，但页面版本、实际连接器、BSP 分支和 IQ 文件仍需在目标板确认。
- 本仓库证据：`docs/isp_migration/hardware/README.md`、`scripts/isp_probe.sh`、`src/media_graph_validator.cpp`。当前没有 board DTS、sensor 驱动或 IQ 文件。

公开参考（用于审计，不等同于当前 BSP 证据）：[Sony IMX415 产品资料](https://www.sony-semicon.com/files/62/pdf/p-12_IMX415-AAQR_AAMR_Flyer.pdf)、[IMX415 数据手册](https://dl.khadas.com/products/add-ons/cameras/imx415/datasheet/imx415-datasheet.pdf)、[Firefly IMX415/RK3588 文档](https://wiki.t-firefly.com/en/IMX415/imx415.html)。

模组网页在不同版本/库存中可能出现不同 Sensor SKU；采购时必须以模组标签、I2C chip ID 和供应商 mode table 复核“IMX415”，不能仅凭商品标题选型。

## 必须现场确认

完整板型号、kernel/BSP commit、MIPI 连接器对应的物理 DPHY、lane 数和 lane 映射、I2C bus/地址、reset/pwdn GPIO 极性、xclk、avdd/dvdd/dovdd、双路是否共享 DPHY、CSI full/split mode、两路 media graph 以及实际 30 fps 带宽。

## 实施顺序

1. 单路：I2C probe → sensor stream → media graph → RAW10 → RKISP NV12 → rkAIQ/IQ → 30 分钟稳定性。
2. 双路：第二颗 Sensor 使用独立地址或隔离总线，确认第二路 DPHY/CSI 和独立 AIQ context 后再启用。
3. 没有 FSIN/trigger 线路和示波器/帧时间戳证据时，只能称软件时间戳配帧。

## 调试命令

```bash
scripts/isp_probe.sh | tee /tmp/imx415_probe.log
media-ctl -p -d /dev/media0
v4l2-ctl -d /dev/videoX --all --list-formats-ext
```

## 测试结果与状态

- SOURCE_IMPLEMENTED：探测脚本和只读 media graph validator 已存在。
- BOARD_VERIFIED：NOT_TESTED；当前环境没有 `/dev/media*`、`/dev/video*` 或 RK3588。
- BLOCKED_BY_HARDWARE：板型号、原理图、BSP、模组 mode table/IQ 未提供。

## 面试要点

先证明物理拓扑和 mode table，再写 DTS；SoC 支持多路 CSI 不等于开发板已布线支持双路。
