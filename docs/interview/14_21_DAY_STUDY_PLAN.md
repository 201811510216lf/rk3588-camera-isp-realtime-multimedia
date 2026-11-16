# 21 天项目技术学习计划

每天 2～3 小时：约 60～90 分钟读源码/文档，30～45 分钟手写或复述，30 分钟自测。每周第 7、14、21 天做一次模拟面试。计划围绕当前代码和证据缺口，不把理论学习写成项目实践。

## 第 1 周：真实执行流程与 Camera 生命周期

### Day 1：入口与证据边界
阅读：`main.cpp`、`include/app_options.h`、`lib/app_options.cpp`、`CMakeLists.txt`、`docs/CODEX_EXECUTION_STATUS.md`。知识点：CLI→Config、构建目标、SOURCE/HOST/BOARD 证据分级。掌握标准：能在 3 分钟说清 `main→run→stats`，并指出三项未验证证据。

自测题：
1. 能指出 main.cpp 中解析参数、创建 DualCameraPipelineConfig 和返回码处理的函数位置吗？
2. BUILD_HOST_TESTS、BUILD_BSP_TOOLS、BUILD_RK_DEMO 三个 CMake 选项分别控制什么？
3. 当前工作树与 Git HEAD 的双路版本有什么差异？
4. 哪些证据只能标记 HOST_STATIC_PASS，不能标记 HOST_CXX_NOT_RUN 或 BOARD_NOT_VERIFIED？
5. 如果面试官问“项目是否已经上板”，你会给出哪三条可核查证据？

### Day 2：V4L2 打开与格式协商
阅读：`lib/v4l2_isp_camera.cpp:203-483`。知识点：QUERYCAP、single/multi-planar、S_FMT、G/S_PARM、REQBUFS、MMAP。掌握标准：画出 ioctl 顺序并解释 NV12 与 NV12M 的区别。

自测题：
1. VIDIOC_QUERYCAP、S_FMT、REQBUFS、QUERYBUF、mmap、QBUF、STREAMON 的正确顺序是什么？
2. VIDEO_CAPTURE_MPLANE 与 VIDEO_CAPTURE 的主要数据结构差异是什么？
3. NV12 与 NV12M 的 plane 数和 UV 地址分别如何表示？
4. G_PARM 失败后代码如何处理帧率协商？
5. 为什么 S_FMT 成功仍要检查驱动回写的 width、height、pixelformat 和 plane 信息？

### Day 3：DQBUF/QBUF 与 plane 布局
阅读：`lib/v4l2_isp_camera.cpp:506-698`、`include/camera_frame.h`。知识点：poll、timestamp、bytesused、data_offset、stride、lease。掌握标准：能说明一个 buffer 从 DQBUF 到最后 QBUF 的所有持有者。

自测题：
1. poll 超时、DQBUF 返回 EIO、DQBUF index 越界三种情况分别怎样处理？
2. bytesused、sizeimage、data_offset 和 mmap length 各自限制什么？
3. 为什么不能用 width*height*3/2 直接读取所有 NV12 buffer？
4. 同一个 slot 重复 DQBUF 时当前代码的保护是什么？
5. 一个 CameraFrame 被 pair queue 和 AI queue 同时持有时，何时才允许 QBUF？

### Day 4：停止与异常回收
阅读：`v4l2_isp_camera.cpp:699-730`、`camera_frame.h`、`docs/PIPELINE_AND_OWNERSHIP.md`。知识点：stop_accepting、in-flight、STREAMOFF、munmap、close。掌握标准：能回答“消费者还没释放时 Ctrl+C 怎么办”。

自测题：
1. stop_accepting() 为什么必须早于等待 in-flight 归零？
2. 如果 STREAMOFF 失败，shutdown 是否仍然继续？
3. 为什么要先等待 lease 再 munmap？
4. 队列 close、线程 join、camera.stop 的安全顺序是什么？
5. QBUF 错误被 noexcept 析构吞掉后，应该通过什么统计或日志发现？

### Day 5：软件配帧
阅读：`include/frame_pairer.h`、`lib/frame_pairer.cpp`、`tests/frame_pairer_test.cpp`。知识点：clock domain、tolerance、max_wait、drop-older、hold-last、P99。掌握标准：手算 5 组时间戳的配对结果并解释丢帧原因。

自测题：
1. FramePairer 为什么要求两个 timestamp clock domain 已知且相同？
2. 当 cam0 时间戳 100ms、cam1 有 101ms 和 104ms 两帧时会选哪一帧？
3. max_wait 超时后 drop-older 与 hold-last 的行为有什么不同？
4. 乱序 sequence 如何被识别并计数？
5. P50/P95/P99 delta 统计为什么不能直接证明硬件曝光同步？

### Day 6：双路 Capture→AI 入队
阅读：`dual_camera_pipeline.cpp:238-317`、`bounded_frame_queue.h`。知识点：两路 capture、AI 节流、drop-oldest、shared lease。掌握标准：指出 Capture 线程做了什么、没有做什么。

自测题：
1. capture_loop 为什么不直接调用 RKNN 或 MPP？
2. ai_fps 如何限制提交频率，队列满时丢弃哪一帧？
3. FramePairer 输出的 FramePair 如何进入 encode loop？
4. camera dequeue 连续失败达到阈值后会发生什么？
5. capture 线程结束后为什么还要 tick 一次 pairer？

### Day 7：第一次模拟面
复习 Day 1–6；完成 `09_MOCK_INTERVIEWS.md` 第一轮。自测题：
1. NV12 与 NV12M 的 plane 数如何判断？
2. 为什么 timestamp 接近不代表同时曝光？
3. lease 最后一次释放触发什么？
4. drop-oldest 会牺牲什么？
5. 当前项目哪些部分没有板端证据？
掌握标准：连续回答 10 分钟，不能把 MMAP 说成零拷贝或软件配帧说成硬件同步。

## 第 2 周：图像、AI、编码与分发

### Day 8：RGA/CPU 图像路径
阅读：`dual_camera_pipeline.cpp:48-100,319-364`、`composite_processor.cpp`、`tests/composite_processor_test.cpp`。知识点：stride copy、NV12↔BGR、resize、ROI、letterbox、坐标映射。掌握标准：解释为何要先 copy 再转换，以及框如何映射到左右 ROI。

自测题：
1. copy_nv12 如何处理 Y 与 UV 的 stride 差异？
2. RGA imcvtcolor 失败时 OpenCV fallback 的代价是什么？
3. 左右 view 的目标 ROI 如何影响检测框坐标？
4. 为什么 BGR→NV12 采用 I420 中间布局再交错 UV？
5. 当前代码是否验证了 RGA 的 DMA-BUF import、cache 和 fence？

### Day 9：RKNN wrapper
阅读：`include/rknnPool.hpp`、`lib/postprocess.cpp`、`tests` 中 AI 相关调用。知识点：rknn_init、core mask、输入 fmt、量化输出、YOLO/NMS。掌握标准：指出 pipeline 传入 `n=0` 的事实、CLI core 参数未生效，以及 NCHW/NHWC 风险。

自测题：
1. rknn_lite 构造函数为什么要求一个输入和至少三个输出？
2. wrapper 中 rknn_set_core_mask 已存在，但 pipeline 为什么仍不能声称 core0/core1 生效？
3. INT8 输出的 zero-point 和 scale 在哪里进入后处理？
4. NCHW 模型会遇到什么输入格式风险？
5. 模型文件不存在时 ai_required=true 和 false 的行为分别是什么？

### Day 10：AI 时序与 OSD
阅读：`dual_camera_pipeline.cpp:286-364`。知识点：AI queue、snapshot、sequence/timestamp 可用性、strict/wait、过期结果。掌握标准：能回答为什么不能把上一帧框直接画到任意新帧。

自测题：
1. AI snapshot 为什么同时保存 sequence 和 timestamp_ns？
2. Strict 模式等待的谓词是什么，超时后画面如何标记？
3. 为什么要要求 snapshot.timestamp_ns <= frame_ts？
4. 左右相机的检测框如何避免画到另一侧 ROI？
5. 推理结果晚到一帧时，当前实现会丢框、复用旧框还是等待？

### Day 11：MPP 编码
阅读：`mpp_encoder.cpp`、`include/mpp_encoder.h`。知识点：MppCtx、输入 buffer、SPS/PPS、packet、timeout、flush/completion。掌握标准：说明已实现调用链和未验证的 buffer ownership。

自测题：
1. MppEncoder::Init 设置了哪些 prep、rate control 和 H.264 参数？
2. GetHeader 返回的 SPS/PPS 在什么时候写入各 sink？
3. encode_put_frame 成功后为什么不能凭接口名立即释放所有输入？
4. timeout、空 packet 和普通编码错误如何区分？
5. 当前代码对 MPP flush/EOS 和 completion 的证据缺口是什么？

### Day 12：H.264 与 packet fan-out
阅读：`encoded_packet_fanout.cpp`、`tests/encoded_packet_fanout_test.cpp`、`dual_camera_pipeline.cpp:366-380`。知识点：Annex-B、IDR、PTS/DTS、shared payload、sink 隔离。掌握标准：能画出一次编码包到 raw/RTMP/MP4 的路径。

自测题：
1. Annex-B 与 AVCC 的 NAL 长度表示有什么区别？
2. EncodedPacketDistributor 如何校验 PTS/DTS 单调性？
3. 为什么 shared_ptr<const vector<uint8_t>> 适合多 sink？
4. sink 返回 false 时会不会让其他 sink 停止？
5. 当前 fan-out 的“共享”是否等于硬件零拷贝？

### Day 13：ffmpeg/RTMP/MP4
阅读：`FfmpegPipe`（`dual_camera_pipeline.cpp:122-174`）、`docs/PERFORMANCE.md`、`docs/BOARD_ACCEPTANCE.md`。知识点：stdin pipe、segment muxer、重连、阻塞风险。掌握标准：明确当前没有真实服务器/ffprobe/播放器验证。

自测题：
1. FfmpegPipe::open_rtmp 使用了哪些输入输出参数？
2. 分段 MP4 的 segment_time 和 reset_timestamps 有什么影响？
3. write 失败时重连几次，当前 packet 如何处理？
4. 为什么同步 fwrite 会把网络/磁盘长尾传回 encode loop？
5. 验证 MP4 可播放和 RTMP 重连需要保存哪些板端证据？

### Day 14：第二次模拟面
完成第二轮项目深挖。自测题：
1. 为什么统一拼接后只编码一次？
2. 共享 packet 如何避免重复拷贝？
3. RTMP 写阻塞会影响哪个线程？
4. MPP timeout 后能否立即释放输入？
5. AI snapshot 的 timestamp 条件是什么？
掌握标准：能从一帧数据讲到编码包，并主动说出三项验证缺口。

## 第 3 周：C++ 并发、压力面与证据补齐

### Day 15：C++ 所有权
阅读：`dual_camera_pipeline.h`、`camera_frame.h`、`bounded_frame_queue.h`。知识点：RAII、unique_ptr、shared_ptr、公开 lease 风险、不可拷贝 pipeline。掌握标准：能解释为什么 pipeline 禁止拷贝以及 owner/lease 尚可拆分。

自测题：
1. 为什么 DualCameraPipeline 禁止拷贝但没有提供移动语义？
2. CameraFrame 的 owner 与 lease 分别保护什么资源？
3. FrameLease 为什么不可拷贝却由 shared_ptr 持有？
4. 公开 owner/lease 字段可能构造出什么不完整状态？
5. RAII 在 MPP pending buffer 语义不确定时能解决什么、不能解决什么？

### Day 16：线程同步与停止
阅读：`dual_camera_pipeline.cpp:178-218,383-451`。知识点：atomic stop、mutex、condition_variable、join、单次 run。掌握标准：指出同步 RKNN 不可取消和 queue drain 风险。

自测题：
1. stop atomic 的可见性和队列 close 的唤醒作用分别是什么？
2. AI mutex、stats mutex、events mutex 各保护哪类数据？
3. 为什么同步 rknn_lite::interf 可能让 join 无界等待？
4. run_started 如何防止同一个实例二次运行？
5. 编码线程在 sink 写入阻塞时，capture 线程会如何间接受影响？

### Day 17：错误处理与可观测性
阅读：`docs/FINAL_ACCEPTANCE_REVIEW.md`、`docs/CODE_REVIEW_REPORT.md`。知识点：错误分级、统计字段、I/O flush、MPP 空 packet、plane 边界。掌握标准：列出至少 5 个 P1 风险及验证方法。

自测题：
1. 如何区分 V4L2 dequeue error、pair drop、AI drop 和 encode error？
2. MPP 返回空 packet 时当前 pipeline 做什么？
3. 为什么最终 ofstream flush/close 也应检查状态？
4. 异常 plane 数或 QUERYBUF plane_count 不一致应如何测试？
5. 真实板端最小日志集合应包含哪些 timestamp、sequence 和 queue 字段？

### Day 18：Camera/ISP 理论扩展
阅读：`docs/BSP_CAMERA_ISP.md`、`docs/HARDWARE_SYNC_FEASIBILITY.md`。知识点：Sensor→CSI→RKCIF/RKISP→V4L2、Media Controller、3A/rkAIQ、IQ、FSIN。掌握标准：能清楚区分“代码依赖 ISP 输出”和“自己实现 ISP/驱动”。

自测题：
1. Sensor、CSI-2、RKCIF、RKISP 和 V4L2 video node 的职责如何分层？
2. 为什么 V4L2 输出 NV12 不能证明自己实现了 ISP？
3. Media Controller graph 验证要保存哪些命令输出？
4. rkAIQ 的 AE、AWB、AF 和 IQ 文件各解决什么问题？
5. 什么 FSIN/Trigger、SOF 或示波器证据才能支持硬件同步结论？

### Day 19：简历与差异化
阅读：本目录 `10–13`、用户提供的三段经历。知识点：岗位映射、证据矩阵、SDK 集成质疑。掌握标准：根据岗位在 30 秒内切换项目重点，不使用未验证数字。

自测题：
1. Camera/ISP 岗位版简历最应该突出哪四项源码能力？
2. Multimedia 岗位为什么把显微镜实习放在双路项目之前？
3. Driver/BSP 岗位如何利用 I2C 体温项目补足当前仓库的缺口？
4. 面试官问“你只是 SDK 集成吗”时，如何指出自己的用户态工程责任？
5. 哪些简历表述必须等待板端或性能证据后才能启用？

### Day 20：压力题演练
阅读：`09_MOCK_INTERVIEWS.md` 第三轮。知识点：双路必要性、USB 对比、NPU core、零拷贝、DTS/ISP 诚实边界。掌握标准：每题先给结论，再给源码证据和局限。

自测题：
1. 为什么双路项目选择 MIPI 而不是 USB Camera，哪些只是设计推断？
2. 为什么两个 AI context 不等于两个 NPU 核并行？
3. 为什么当前路径不能称为零拷贝？
4. 如果 Sensor/DTS/rkAIQ 不在仓库，如何诚实回答个人贡献？
5. 压力面最后应主动列出哪五项未验证能力？

### Day 21：第三次模拟面与收口
完成全流程 20～30 分钟模拟面；复查 `15_RESUME_CLAIM_AUDIT.md`。自测题：
1. 当前最强的三个亮点是什么？
2. 最危险的五个追问是什么？
3. 哪些功能仅 SOURCE_IMPLEMENTED？
4. 为什么硬件 sync fail-fast？
5. 下一次上板最先采哪些日志？
掌握标准：完整讲解不超过 8 分钟，所有关键主张都能指向路径/函数/验证状态。

## 每周模拟面验收

- 第 1 周：能讲清采集、配帧、lease。
- 第 2 周：能讲清 AI、OSD、编码和分发。
- 第 3 周：能在压力追问下主动承认缺口，并提出最小验证步骤。


## 第五轮校准说明（2026-11-13）

学习时把“项目源码能力”和“目标硬件能力”分开验收。第五轮审查确认：Python 结构检查和 analyzer 测试通过，但 CMake/C++/CTest 未执行；没有 Sensor/DTS/rkAIQ/IQ/板端日志。因此 Day 18–21 的验收标准必须包含“能主动说出证据缺口”，而不是背出一个已完成的 ISP 结论。

第 9 天必须额外检查 `include/rknnPool.hpp:191-209` 的 NCHW/NHWC 风险、`dual_camera_pipeline.cpp:435-436` 的 NPU core 参数未生效，以及 `postprocess.cpp:114-149,373-387` 的后处理索引风险。第 11–13 天必须把 MPP completion 未验证、sink 同步阻塞和 ffmpeg/播放器未验证列为追问重点。第 18 天必须能说明 `lib/aiq_controller.cpp:83-124` 是 fail-closed 发现壳，不是真实 rkAIQ backend。
