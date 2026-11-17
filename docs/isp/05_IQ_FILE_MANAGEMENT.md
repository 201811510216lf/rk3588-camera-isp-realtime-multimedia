# IQ 文件管理

## 规则

IQ 文件必须来自厂商，按 Sensor、板级镜头、分辨率/模式和 rkAIQ schema 匹配。原文件只读、实验前哈希备份；版本不匹配时停止加载并记录原因。

## 当前实现与计划

`isp_debug_tool --iq-info` 和 `isp_control_cli --iq-root` 可报告候选路径；`src/aiq_controller.cpp` 不修改 IQ。实际加载 API 待目标 SDK 确认。建议为每路保存 `camera_id/sensor/iQ_path/sha256/sdk_version/load_result` 清单。

## 调试命令

```bash
find /etc/iqfiles -type f -iname '*imx415*' -print
sha256sum /etc/iqfiles/*imx415* 2>/dev/null
python3 scripts/iq_manifest.py --root /etc/iqfiles --sensor imx415 --camera-id 0 --json
isp_debug_tool --iq-info --camera 0 --iq-root /etc/iqfiles
```

## 状态

SOURCE_IMPLEMENTED：只读发现；IQ_LOADED、SDK_COMPATIBLE、BOARD_VERIFIED：NOT_TESTED；NOT_IMPLEMENTED：未生成或修改任何伪造 IQ。

## 面试要点

IQ 不是通用配置文件；Sensor、镜头、BSP/rkAIQ schema 和版本必须同时匹配。
