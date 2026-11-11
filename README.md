# RK3588 Camera ISP Realtime Multimedia

工程保留原有 `legacy` 输入：RTSP/MP4 → FFmpeg → MPP Decode → RGA/RKNN → 四宫格 → `StreamingManager`。默认模式仍是 legacy，旧的数字路数调用形式也保留：

legacy 的现有可用输出是 RTMP/FFmpeg FLV 封装；代码会对未实现的 RTSP 输出明确报错，不把空实现当作成功。

```bash
./build/myDemo 4
```

新增 `mipi` 模式用于单路板端 Camera：ISP 已处理的 V4L2 NV12/NV12M → MPP H.264 → raw H.264 文件。AI 是可选的单模型分支，录像编码先于 AI 提交；AI 队列容量为 2，过载时统计并丢旧帧。

```bash
./build/myDemo --source=mipi --device=/dev/videoX \
  --width=1920 --height=1080 --fps=30 \
  --output=/data/camera.h264 --ai-enable=false --duration-sec=60

./build/myDemo --source=mipi --device=/dev/videoX \
  --output=/data/camera.h264 --ai-enable=true \
  --model=/data/models/person_relu.rknn --ai-fps=5 \
  --events=/data/camera_events.jsonl --duration-sec=60
```

`/dev/videoX` 必须由 `media-ctl -p` 和 `v4l2-ctl --list-devices` 确认是 ISP NV12 输出节点；RAW Bayer 节点不能直接作为输入。程序会协商 single/multi-planar 格式、记录 stride/plane/DMA-BUF export，并在无法导出 DMA-BUF 时明确使用 mmap + copy。当前 MPP 封装只对 NV12 做有界转换拷贝，没有宣称零拷贝。

## 构建和测试

板端/交叉 Linux 环境（已准备 RKNN、MPP、RGA、FFmpeg-lite、OpenCV 兄弟目录）执行：

```bash
cmake -S . -B build -DREQUIRE_RK_DEMO=ON -DBUILD_HOST_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

只有 host mock 测试时：

```bash
cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

当前工作环境没有 CMake/C++ 编译器和 RK 依赖，构建结果见 [`docs/isp_migration/04_test_report.md`](docs/isp_migration/04_test_report.md)。板端只读探测使用：

```bash
scripts/isp_probe.sh | tee isp_probe.log
```

Buffer lease、V4L2 stop 顺序和双分支退出约束见 [`docs/isp_migration/02_buffer_lifecycle.md`](docs/isp_migration/02_buffer_lifecycle.md)；Sensor/CSI/ISP/rkAIQ 调试清单见 [`docs/isp_migration/03_isp_bringup.md`](docs/isp_migration/03_isp_bringup.md)。

当前交付状态：`IMPLEMENTED_AWAITING_HARDWARE`。尚未取得 RK3588/MIPI Sensor/BSP/rkAIQ 的实测证据，不把软件编译或 mock 结果等同于 ISP 硬件验证。
