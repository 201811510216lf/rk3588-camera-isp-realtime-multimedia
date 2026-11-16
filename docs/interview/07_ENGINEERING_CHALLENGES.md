# 工程难点与面试讲解

以下难点均按当前源码解释，并明确已验证程度。

## 难点一：双路时间戳配帧

**背景**：两路 Camera 到达时间不同，AI/拼接需要选择相近帧。  
**设计**：`FramePairer` 按 camera id、sequence、timestamp、clock domain 和 tolerance 管理两个 deque；`drain_ready_locked` 搜索全队列最小 delta，`expire_locked` 处理超时。源码：`lib/frame_pairer.cpp:88-194`。

**为什么不用简单 front-to-front**：捕获抖动会让更近的候选已经在队列中，盲配 front 可能错配。  
**验证**：`tests/frame_pairer_test.cpp` 覆盖匹配、超时、乱序、clock domain、队列深度。C++ 测试源码存在，但本机没有 C++ 构建工具。  
**局限**：Unknown clock domain 被拒绝；软件配帧不代表同步曝光；没有 FSIN/Trigger 证据。

30 秒回答：我用驱动 timestamp 和 sequence 做软件配帧，FramePairer 有容差、超时和 drop policy；硬件同步需要独立的 Sensor trigger 证据，当前没有宣称。

追问：时钟域不同怎么办？为什么不能直接相减？HoldLast 会不会重复帧？如何统计 pair drop？

## 难点二：Camera Buffer 生命周期

**背景**：V4L2 slot 由驱动复用，Capture、AI、拼接可能同时引用。  
**设计**：`CameraFrame` 携带 `FrameLease`；`BufferLeaseState::acquire/release` 用 in-flight 计数，最后一个 lease 才执行 QBUF（`include/camera_frame.h:67-142`）。停止先 `stop_accepting` 和 `wait_for_zero`，再 STREAMOFF。

**验证**：`tests/camera_lifecycle_test.cpp:12-48` 覆盖最后一个 copy 才 requeue、停止后禁止 QBUF。  
**潜在 Bug**：owner/lease 可被公开拆分；QBUF 错误在 noexcept release 中只能被吞掉；V4L2 multi-plane 协议需要更严格契约。

30 秒回答：我不把 CameraFrame 析构简单等同于硬件完成，而是用共享 lease 保证所有消费者退出后才 QBUF；停止路径先禁止新 lease，再等待归零。

## 难点三：采集、AI、编码背压

**背景**：AI 慢不能拖死录像，队列无限增长又会耗尽内存。  
**设计**：AI queue 容量 2，满时丢旧；pair queue/AI queue 有 close；Capture、AI、Encode 分线程。源码：`include/bounded_frame_queue.h`、`lib/dual_camera_pipeline.cpp:238-379`。

**局限**：RTMP/MP4 sink 仍同步写在 encode thread；慢 sink 会阻塞编码和采集；AI/NPU 调用没有取消机制。

## 难点四：NV12/NV12M 多平面布局

**背景**：不同 V4L2 节点可能是 single-planar 或 multi-planar，stride/padding/offset 不同。  
**设计**：`V4L2IspCamera::open` 协商格式、记录 plane/stride/sizeimage；`dequeue` 校验 bytesused/offset；`copy_nv12` 逐行打包。源码：`lib/v4l2_isp_camera.cpp:247-380,587-680`、`lib/dual_camera_pipeline.cpp:48-75`。

**局限**：当前审查发现 QUERYBUF plane count 允许少于协商 count，属于 P1；应补 mock layout 测试。

## 难点五：RKNN 结果与视频时序

**背景**：推理耗时可能超过一帧，旧检测框不能无条件画到新帧。  
**设计**：AIItem 记录 sequence/timestamp；AiSnapshot 保存结果；compose 时用 timestamp/sequence 判断是否可用（`dual_camera_pipeline.cpp:336-358`）。

**局限**：NCHW 输入布局处理可能错误；两个模型当前都固定 core0；后处理 NMS/confidence 索引有风险。

## 难点六：RGA/CPU 与拼接 OSD

**背景**：两路 NV12 需要转换、缩放、拼接和框映射。  
**设计**：NV12→BGR 使用 RGA，失败时 OpenCV fallback；`CompositeProcessor::map_box/clip_box` 映射框；canvas 上绘制 OSD，再 BGR→NV12。源码：`dual_camera_pipeline.cpp:319-364`、`composite_processor.cpp:70-138`。

**验证边界**：没有目标板 RGA DMA-BUF import、cache/fence 或性能数据；不能声称零拷贝/硬件加速收益已测。

## 难点七：一次编码、多 sink

**背景**：同时需要 raw H.264、RTMP 和 MP4，重复编码浪费资源。  
**设计**：`EncodedPacketDistributor` 共享 packet bytes，并按 PTS/DTS 单调性分发（`encoded_packet_fanout.cpp:15-49`）。  
**局限**：sink 同步执行，写失败不传播，broken pipe 重试可能重复 packet；这是 P1。

## 难点八：ISP/rkAIQ 真实性边界

**背景**：Camera 岗位最容易把用户态 V4L2 误说成 Sensor/ISP 开发。  
**真实回答**：当前仓库有 V4L2 output capture、只读 media graph、RAW 分析和 fail-closed AIQ controller，但无 IMX415 kernel driver、DTS、IQ、rkAIQ context 或 AE/AWB 回读。源码依据：`lib/aiq_controller.cpp:83-124`、`docs/isp/13`。

## 建议的 2 分钟统一回答

“我先从 `main.cpp` 解析参数并构造 `DualCameraPipeline`。Pipeline 配置两路 V4L2 node，`V4L2IspCamera` 做 capability、format、REQBUFS、MMAP 和 DQBUF。每帧带 sequence、timestamp、plane layout 和 lease，FramePairer 在相同 clock domain 内按容差配对。AI 分支复制 NV12 后走 RGA/OpenCV 和 RKNN，结果按 sequence/timestamp 与拼接帧关联；编码分支把两路转换、缩放、OSD 后统一成一帧 NV12，交给一个 MPP H.264 encoder，再把 packet 分发到 raw/RTMP/MP4。当前这些是用户态源码路径；IMX415 Sensor、DTS、RKISP、rkAIQ、IQ 和板端稳定性都没有在本仓库验证，所以我会把它描述为待板端验证的工程，而不是已完成硬件 bring-up。”
