# Linux Multimedia：V4L2、DMA-BUF、RGA、MPP 与输出

## 1. YUV 与 NV12/NV12M

NV12 是 Y plane 后跟交错 UV plane；NV12M 是两个独立 plane。每行可能有 padding，真正可读范围由 stride、bytesused、data_offset 和 sizeimage 共同约束。I420/YUV420P 则是独立 Y/U/V plane。

项目路径：`lib/v4l2_isp_camera.cpp:587-680` 校验每个 plane 的 offset、bytesused 和 negotiated layout；`lib/dual_camera_pipeline.cpp:48-75` 将 stride 帧复制成 packed NV12；`lib/mpp_encoder.cpp:248-307` 将 NV12 转为 encoder 使用的 I420 workspace。

面试重点：不能只按 `width*height*3/2` 读取 DMA buffer；必须尊重 stride 和 plane offset。当前审查仍发现 QUERYBUF plane 数契约需要收紧，见 `docs/isp/14_IMX415_FINAL_CODE_REVIEW.md`。

## 2. mmap、DMA-BUF 与零拷贝

`MMAP` 是用户态映射驱动 buffer；`EXPBUF` 导出 DMA-BUF fd，允许其他硬件模块导入。导出 fd 不等于零拷贝，仍需验证 RGA/RKNN/MPP 的 import API、cache coherency、fence 和 completion。

当前实现：`lib/v4l2_isp_camera.cpp:437-446` 尝试 `EXPBUF`，但 pipeline 在 `copy_nv12()` 中逐行 CPU copy，MPP 再申请自己的 buffer。面试应明确：这是保守 copy path，不能写成全链路零拷贝。

扩展学习：DMA-HEAP、dma-buf attach/map、IOMMU、cache flush/invalidate 和同步 fence；这些不代表当前项目已经实现。

## 3. RGA

RGA 适合 resize、crop、颜色转换和 compose，通常比 CPU 逐像素处理更适合嵌入式图像路径。需要正确传入 virtual/fd buffer、format、width/height、stride 和 plane 布局。

项目路径：`lib/dual_camera_pipeline.cpp:77-85` 做 NV12→BGR；`:327-333` 用 `imresize` 缩放；`:363` 通过 `bgr_to_nv12` 输出拼接结果。失败时部分路径回退 OpenCV。

常见追问：为什么不用 OpenCV？

回答：RGA 更贴合 RK3588 硬件图像搬运和颜色转换，CPU/OpenCV 作为兼容 fallback；但当前代码没有目标板 DMA-BUF import 和性能数据，因此不能宣称 RGA 已带来实测收益。

## 4. H.264 与 MPP

H.264 编码器产生 SPS/PPS、IDR、非 IDR slice，并通过 GOP 和码率控制影响延迟与码流大小。MPP 是 Rockchip 的媒体处理接口，应用通常配置 `prep:*`、`rc:*`、codec profile，再提交 frame、获取 packet。

项目路径：`lib/mpp_encoder.cpp:42-197` 初始化 MPP、配置 H.264/VBR/帧率/GOP；`:322-437` 将 I420 frame 提交并轮询 packet；`:440-474` 获取 extra info（SPS/PPS）。

关键边界：`encode_put_frame` 返回不一定代表硬件停止访问输入 buffer。当前代码在 timeout/error 时保留 pending buffer 到 context destroy，但目标 BSP 的完成语义尚未验证。

## 5. PTS/DTS、Annex-B、RTMP 与 MP4

PTS 表示展示时间，DTS 表示解码时间；H.264 Annex-B 使用 start code，MP4/AVCC 常把 SPS/PPS 和 NAL 长度放入封装结构。RTMP 通常通过 FLV 传输 H.264，MP4 是容器，需要 muxer 或 ffmpeg 正确写时间戳和索引。

当前 `DualCameraPipeline` 使用递增 `pts`（`lib/dual_camera_pipeline.cpp:366-378`），通过 `EncodedPacketDistributor` 把同一 packet 送到 raw、RTMP、MP4 sink。`FfmpegPipe` 位于 `:122-174`。

不能说“启动 ffmpeg 就推流成功”。当前没有 server/player/ffprobe 证据，且 sink 写错误不会充分传播，见 `docs/isp/14` P1-003。

## 6. 一次编码，多目的分发

`include/encoded_packet_fanout.h` 的 `EncodedPacket` 用 `shared_ptr<const vector<uint8_t>>` 共享 packet bytes；`lib/encoded_packet_fanout.cpp:15-49` 校验 PTS/DTS 单调性并顺序调用 sink。

设计价值：避免为 RTMP、MP4、raw 各编码一次。当前局限：sink 是同步调用，慢网络或磁盘会阻塞编码线程；更强设计需要每 sink 独立有界队列、超时和失败策略。

## 7. 线程、背压和丢帧

`BoundedFrameQueue` 容量满时 drop-oldest，AI 允许丢旧帧；`DualCameraPipeline` 有 pair queue、两个 AI queue 和 encode thread。采集到的 CameraFrame 仍通过 shared lease 保护，复制/转换完成后才归还。

面试回答要区分：

- capture drop：采集或配帧阶段丢失；
- AI skip/drop：为了限频或队列满而丢；
- encode error/drop：编码或输出失败；
- sink error：RTMP/MP4/raw 写失败。

当前统计仍有 `pair_frames=encoded_frames` 等语义问题，不应把统计字段当实测性能。

## 8. 典型调试方法

1. `media-ctl -p`、`v4l2-ctl --all` 确认 node/format。
2. 打印 negotiated width/height/stride/plane count/sizeimage。
3. 检查 DQBUF sequence/timestamp/flags。
4. 用小尺寸 NV12 样本验证 plane offset 和 UV 起点。
5. 先关闭 AI/RTMP/MP4，验证 Camera→raw H.264。
6. 再单独加入 RGA/RKNN、MP4、RTMP，并保存退出码、pclose/ffprobe 日志。

## 9. 扩展学习

RTP/RTSP/WebRTC、FLV/MP4 mux、H.264 CAVLC/CABAC、B-frame reorder、硬件 fence、MPP asynchronous task API 和 GStreamer 是岗位相关知识，但不代表本仓库已实现。
