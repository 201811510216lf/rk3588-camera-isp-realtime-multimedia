# 阶段 6：视频处理、OSD 与 MPP

已读取阶段 4/5 报告，复用原 RGA 转换、CompositeProcessor 和 MppEncoder，不添加 MJPEG Decode，不接 Software ISP 到 RKISP NV12 主链路。

`--video-mode joint` 保留双视角软件配帧和合成；`independent` 新增两路独立有界录像队列、编码线程、MPP context 和输出组，录像不等待另一 Camera。单路采集故障在 independent 模式隔离；设备完全不可用时明确失败。录像队列溢出停止采集并报告错误，不静默丢弃实验录像。joint 本身按配帧策略选择/丢弃输入，不能当两路原始完整录像使用。

图像读取复用 camera::copy_nv12_packed，按实际 NV12/NV12M plane、stride、offset 和 bytesused 读取再打包。RGA 转换/联合模式缩放尝试实际 adapter；OpenCV fallback/独立模式 CPU resize 均明确是 CPU 处理。输入 lease 在图像复制完毕后释放；无 DMA-BUF 零拷贝声明。

OSD 检测只使用同 Camera、同 sequence、同源时间戳结果；异步旧快照不绘制为当前检测，录像不会等待 AI。显示 Camera、源 sequence/timestamp、类别和置信度。没有行为结果时明确 UNAVAILABLE。模型推理使用严格 sidecar 参考预处理张量；主线程把真实检测送入 tracker、显式行为类别映射和时间事件管理器，未配置行为映射不生成行为。

MPP 输入工作区仍为 packed I420，提交到 MPP buffer 时新增 16 对齐 hor/ver stride、逐 plane 拷贝及 padding，避免把 1080 有效高度当作对齐后 storage height。拒绝奇数尺寸、短帧、溢出尺寸/码率；保留超时输入直到 context teardown 的生命周期边界。每个关键帧附加 SPS/PPS，供分段和网络恢复。

真实验证：主代理在 Windows Ninja/MSVC Debug 构建后报告 17/17 CTest 通过（含 output_pipeline_test 和 software_integration_test）。这些主机测试不编译厂商视频实现。RGA/MPP/RKNN SDK 构建、对齐约束、硬件完成语义、首个 IDR、吞吐均为 `BLOCKED_HARDWARE`。

仍为 PARTIAL：MPP wrapper 保留同步 put/get 流程，没有经过真实 SDK 验证的独立 Encode Input/Output worker、显式 MPP PTS/flush/EOS 后端。包层使用有理 CFR 时间基，源时间和输出帧序号通过索引关联；不能将该索引宣称为硬件编码器 PTS 原样传递。相关板端门槛必须见最终适配清单。
