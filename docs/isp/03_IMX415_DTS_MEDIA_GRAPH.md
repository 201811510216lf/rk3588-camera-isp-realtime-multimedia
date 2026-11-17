# IMX415 DTS / CSI-2 / RKISP Media Graph

## 原理

DTS 通过 sensor endpoint 的 `data-lanes`、`link-frequencies`、`remote-endpoint` 把 I2C sensor 接到 CSI-2 DPHY、RKCIF 和 RKISP。每个 endpoint 必须双向匹配，lane、极性、时钟和供电需与原理图一致。

## 当前仓库证据

没有目标板 DTS，因此没有直接修改 BSP 文件。只读检查器位于 `src/media_graph_validator.cpp`，入口为 `tools/media_graph_validator_main.cpp`；脚本为 `scripts/isp_probe.sh`。它们只读取 `media-ctl -p` 和 `v4l2-ctl` 输出。

## 可审查模板

`docs/isp/patches/imx415-rk3588-overlay-template.dts` 是占位 overlay，所有板级值以 `__BOARD_SPECIFIC__` 标记；它不能在未知板上直接编译/应用。拿到板级 DTS 后，应生成独立 git patch，保留原文件备份并先用 `dtc -I dts -O dtb` 静态编译。

## 关键节点

`imx415@<i2c-address>`、`csi2_dphy`、`mipi_csi2`、`rkcif`、`rkisp`、`port/endpoint`。必须核对 `compatible/reg/clocks/clock-names/reset-gpios/pwdn-gpios/*-supply/data-lanes/link-frequencies`。

## 调试命令

```bash
media-ctl -p -d /dev/media0
v4l2-ctl -d /dev/videoX --all --list-formats-ext
./build/media_graph_validator --media-device /dev/media0 --camera0 /dev/videoX --camera1 /dev/videoY --width 3840 --height 2160 --fps 30
```

## 状态

SOURCE_IMPLEMENTED：模板和只读 validator；NOT_TESTED：dtc、目标 kernel、实际 graph；BLOCKED_BY_HARDWARE：未知板级 endpoint/lane/GPIO/供电。

## 面试要点

区分 SoC 的物理 DPHY 能力、板级布线和逻辑 full/split mode；两个 video node 出现也不代表两颗 Sensor 已独立曝光。
