# Linux Multimedia / 音视频岗位简历版本

## 证据边界

当前源码具备 RGA/OpenCV 转换、单 MPP H.264 编码、Annex-B 包 fan-out 和 ffmpeg 管道封装路径。没有 C++ 编译、RK3588 运行、ffprobe/播放器或真实 RTMP 服务器证据；所有 FPS、延迟、重连稳定性均为 `NOT_MEASURED`。

## 项目名称与技术栈

**RK3588 双路相机实时拼接、AI 叠加与 H.264 分发**
技术栈：C++17、V4L2、NV12、RGA、OpenCV、MPP H.264、SPS/PPS、IDR、PTS/DTS、ffmpeg pipe、RTMP、分段 MP4、有界队列。

## 80～120 字项目描述（当前已验证版）

围绕双路相机实时视频输出，设计从 V4L2 NV12/NV12M 采集、RGA/CPU 色彩转换与左右拼接，到单路 MPP H.264 编码和 raw/RTMP/分段 MP4 多 sink 分发的用户态链路。通过共享 `EncodedPacket` 避免重复复制 payload，并记录 sink 错误；当前 sink 仍在编码线程同步写入，错误传播和目标板性能/网络封装仍待验证。

## 四条核心技术工作

1. `lib/dual_camera_pipeline.cpp:319-364` 将两路 NV12 转为 BGR，使用 RGA resize、OpenCV fallback 和布局映射生成 side-by-side 画面，并把 AI 框映射到目标 ROI。
2. `lib/mpp_encoder.cpp:42-309,322-439,440-475` 完成 MPP 初始化、输入工作 buffer、H.264 编码、packet/header 获取和错误返回；`GetHeader` 输出 SPS/PPS，header 写入各 sink 前进行错误检查。
3. `lib/encoded_packet_fanout.cpp:5-62` 用 `shared_ptr<const vector<uint8_t>>` 共享一份编码 payload，校验 PTS/DTS 单调并逐 sink 统计错误；当前调用仍是同步的，`publish(false)` 未完整传播到 pipeline，raw、RTMP、MP4 的实际输出仍需板端验证。
4. `FfmpegPipe`（`lib/dual_camera_pipeline.cpp:122-174`）通过 ffmpeg stdin 写入 RTMP 和分段 MP4，断管时尝试重连一次；`fwrite` 在 encode 线程同步执行，因此网络或磁盘长尾仍可能阻塞编码。

## 面试追问与回答边界

| 追问 | 基于证据的回答 |
|---|---|
| 为什么只用一个编码器？ | 拼接后输出只有一张合成画面，单 MPP 编码器减少重复编码和码率管理；这是当前设计选择，尚未有双编码器性能对比数据。 |
| RTMP/MP4 是否共享编码结果？ | 是，`EncodedPacketDistributor` 共享同一 payload；但 sink 写入在编码线程同步调用，不能说网络和录像完全解耦。 |
| 你验证过 MP4 可播放吗？ | 没有。当前只实现 ffmpeg segment 命令和错误计数，缺少板端 ffprobe/播放器日志。 |
| PTS/DTS 从哪里来？ | fan-out 入口要求单调；实际 ffmpeg stdin 命令只接收 Annex-B 字节流，精确时间基和服务端时钟行为待实测。 |

## 目标完成后版本（仅草稿）

补齐 MPP completion/flush、真实 PTS/DTS、ffprobe、RTMP 断网重连和长稳数据后，才可加入“实时 30 FPS”“低延迟”“稳定推流”等定量表述。当前不得编造这些数字。

## 对应证据文件

`lib/dual_camera_pipeline.cpp:319-380,383-451`、`lib/mpp_encoder.cpp`、`lib/encoded_packet_fanout.cpp`、`include/mpp_encoder.h`、`include/encoded_packet_fanout.h`、`docs/PIPELINE_AND_OWNERSHIP.md`、`docs/PERFORMANCE.md`、`tests/encoded_packet_fanout_test.cpp`。

