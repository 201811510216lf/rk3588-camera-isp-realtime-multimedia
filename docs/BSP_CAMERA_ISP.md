# BSP、Sensor、Media Graph 与 rkAIQ 适配状态

状态：`SOURCE_IMPLEMENTED / BOARD_TESTED=NOT_MEASURED`（2026-11-12，Asia/Shanghai）。

本执行环境只包含本应用仓库；没有可访问的 RK3588 BSP checkout、Linux kernel tree、Sensor 模组资料、`/dev/media*`、`/dev/video*`、rkAIQ headers/libraries 或 IQ 文件。因此本文件不填写 Sensor 型号、I2C 地址、GPIO、MIPI lane 或不存在的 rkAIQ 函数名。把未知值写成“已适配”会造成错误的硬件结论。

实际审计了 `E:\embedded\new _project` 下的相邻工程和本仓库递归文件，没有发现 `.dts/.dtsi`、rkAIQ/rkISP 头文件或 BSP checkout；当前 Windows 环境也没有 Linux 设备节点。

## 已交付的真实软件

- `V4L2IspCamera` 仍负责 NV12/NV12M capability/format/plane/stride/bytesused/timestamp 校验、MMAP lease 和安全停止。
- `media_graph_validator` 是只读校验工具。它调用目标板上已有的 `media-ctl -p` 和 `v4l2-ctl --all --list-formats-ext`，或读取由板端保存的原始命令输出；不会写 link、格式、control 或 stream 状态。
- `isp_control_cli`/`AiqController` 只做服务/运行库/IQ 根目录发现和状态转储。由于没有真实 BSP API，AE、曝光、增益、AWB 写入会返回 `unsupported` 并 fail-closed，不回显假成功。
- `scripts/isp_probe.sh` 提供完整只读环境采集；`scripts/isp_capture_experiment.sh` 对已核验的 ISP 输出节点执行有界 NV12 抓帧；`scripts/analyze_isp_samples.py` 从真实 NV12 和可选 DQBUF CSV 计算亮度/噪声/时间间隔。
- `scripts/aiq_sample_capture.sh` 采集 daemon/runtime/IQ 状态并保留 AE/AWB 字段；在没有真实 rkAIQ backend 时这些字段明确输出 `NOT_MEASURED`。

## 目标板采集步骤

先在板端执行并保存原始输出：

```bash
scripts/isp_probe.sh | tee docs/isp_migration/hardware/isp_probe_$(date +%Y%m%d_%H%M%S).log
media-ctl -p -d /dev/media0 | tee docs/isp_migration/hardware/media_graph.txt
v4l2-ctl --list-devices | tee docs/isp_migration/hardware/v4l2_devices.txt
```

从 `media-ctl -p` 识别两个独立的 ISP 输出节点后，运行：

```bash
./build/media_graph_validator \
  --media-device /dev/media0 \
  --camera0 /dev/videoX --camera1 /dev/videoY \
  --width 1920 --height 1080 --fps 30
```

工具必须报告两个不同节点、两个节点都列出 NV12/NV12M、尺寸和帧率匹配；任一条件失败都返回非零退出码。若板端只能提供离线证据，可使用 `--graph-file`、`--camera0-info`、`--camera1-info` 指向未修改的命令输出。

## Sensor driver/DTS 变更边界

在拿到真实 BSP 前，仓库不生成 `bsp_patches/`，因为没有可验证的上游文件上下文。获得 BSP 后必须记录：

1. Sensor driver 路径、compatible、I2C 地址、regulator/reset/pwdn/MCLK、支持的 mode table 和 exposure/gain controls。
2. 板级 DTS/DTSI 中两个 Sensor 节点、`remote-endpoint`、`data-lanes`、pinctrl、时钟与供电，以及到 RKCIF/RKISP 的实际路由。
3. `media-ctl -p` 中 entity/link 的启用状态和对应 video 节点；不能把 RAW 节点当作 NV12 ISP 输出。
4. 内核/BSP commit、交叉编译命令、dtb/module 安装位置和回滚命令。

补丁应以实际 checkout 为基准，单独保存为可应用 patch；禁止根据“常见 RK3588 配置”猜测 GPIO、地址或 Sensor 名称。

## rkAIQ 控制边界

`AiqController` 当前只提供状态发现和证据转储。`--ae-auto`、`--exposure-us`、`--gain`、`--awb-auto` 在未注入板级 backend 时明确返回 `unsupported`。获得 BSP API 后，需在单独 backend 中实现并记录：

- 已有 rkAIQ daemon/service 是否拥有 ISP；同一 ISP 不得被第二个进程重复 init。
- AE/AWB 自动/手动设置、曝光/增益设置与回读、失败码和 stop/deinit 生命周期。
- Sensor/IQ 文件匹配、IQ 版本和加载状态；未匹配时拒绝运行而不是只打印配置值。

控制验证命令（仅在 backend 已安装后）：

```bash
./build/isp_control_cli --isp-status --camera-id 0 --iq-root /etc/iqfiles
./build/isp_control_cli --ae-auto --camera-id 0
./build/isp_control_cli --exposure-us 10000 --camera-id 0
./build/isp_control_cli --gain 1.0 --camera-id 0
./build/isp_control_cli --awb-auto --camera-id 0
./build/isp_control_cli --dump-isp-state /data/cam0_isp_state.txt --camera-id 0
```

没有真实板端输出时，AE、AWB、曝光时间、增益和 IQ 匹配均记为 `NOT_MEASURED`。

