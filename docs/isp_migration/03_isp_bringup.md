# G5：RK3588 Camera/ISP 板端 bring-up

本文件只描述需要真实板端证据的步骤。工程接收的是 ISP 已处理的 NV12/NV12M 视频节点，不把 RAW Bayer 当作 NV12，也不在没有 BSP 证据时硬编码 `/dev/video0` 或 rkAIQ API。

## 只读探测

在目标板保存完整输出：

```bash
uname -a
cat /proc/device-tree/model 2>/dev/null || true
chmod +x scripts/isp_probe.sh
scripts/isp_probe.sh | tee isp_probe_$(date +%Y%m%d_%H%M%S).log
```

脚本只读取内核、media graph、V4L2 capability/format、dmesg 和可见 rkAIQ 库/进程；不会写 DTS、IQ 文件、格式或控制值。

## 需要确认的证据

1. RK3588/3588S 型号、BSP/内核版本和实际 Sensor driver。
2. `media-ctl -p` 中 Sensor→CSI→CIF→ISP 的 link 是否 enabled，Sensor mode、RAW pattern、lane 数和 link status。
3. 对应 ISP mainpath/selfpath 节点的 `v4l2-ctl --all`、`--list-formats-ext`：确认节点真正输出 `NV12`/`NV12M`，buffer type、bytesperline、sizeimage、plane 数和可否 `VIDIOC_EXPBUF`。
4. IQ 文件位置、rkAIQ 服务/守护进程和版本必须来自当前 BSP 文档或探测结果；没有这些证据时保留 `TODO(board-specific)`，不填写函数名或路径。

## 运行顺序

```bash
# 先确认设备和格式，再以录像优先运行
./build/myDemo --source=mipi --device=/dev/videoX --width=1920 --height=1080 \
  --fps=30 --output=/data/camera.h264 --ai-enable=false --duration-sec=60

# 之后才启用一个已存在且已在该板验证过的模型
./build/myDemo --source=mipi --device=/dev/videoX --output=/data/camera.h264 \
  --ai-enable=true --model=/data/models/person_relu.rknn --ai-fps=5 \
  --events=/data/camera_events.jsonl --duration-sec=60
```

输出是 raw H.264 elementary stream；未验证 MP4 封装能力前不要把它命名为 `.mp4`。如果 DMA-BUF export 不可用，程序会报告 `memory=MMAP dma_buf_export=no` 并使用有界拷贝路径。

## AE/AWB/曝光控制边界

自动曝光/白平衡和手动 exposure/gain 的 ioctl 或 rkAIQ API 随 BSP 版本变化。当前机器没有板端 SDK，因此工程不加入空实现。板端具备对应 API 后，应记录：镜头/灯光/距离、shutter、gain、帧率、平均亮度、实际回读值和识别事件；没有标注数据不报告准确率提升。
