# 大鼠行为结果的证据边界

检测、跟踪、分类和时间事件是不同层。`DetectionResult` 包含 camera_id、原始 sequence/source timestamp、本机 monotonic timestamp、bbox/class_id/confidence 与 model_id/version。IoU tracker 只提供 Camera 内临时 track_id，遮挡和交叉可能导致 ID 切换；它不支持个体身份，不能自动填 rat_id。

行为标签只来自模型 sidecar 中明确的 `behavior_classes=0:grooming,1:rearing` 映射。普通大鼠检测模型设 `behavior_classes=`，不输出虚构行为。此映射表示操作者声明模型的类别语义，不证明模型能识别持续行为；实验定义、标注、模型训练和验证必须另行完成。Demo 结果在 tracker/event 入口被拒绝。

`EventManager` 使用 monotonic time 计算持续时间，source timestamp 留作原始证据。按 camera_id/track_id 隔离候选状态，满足最小观测次数和持续时间后，才在漏检 grace 到期、标签/模型版本变化或 shutdown 时输出最终事件。短漏检会合并，超过 grace 后成为新事件；事件结束时间是最后一次有效模型观测时间。低置信度、NaN、重复/乱序帧被拒绝，状态数有界。阈值由 EventConfig 明确设置。

事件包含独立会话 event_id、Camera、track_id、可选 rat_id、行为、源/monotonic 起止时间、duration/confidence、首尾 sequence、model id/version。JSONL 和 CSV 序列化均转义文本。视频关联允许多个 VideoSpan，记录实际 segment_id、分段内起止 offset 和帧序号；跨分段事件不可只填一个估计文件名。仅在录制索引已证明该分段存在时调用 `add_video_span`，缺失关联保留空数组，不能假称已录像。后续可根据事件的 monotonic/sequence 与真实录像索引补充关联。

主机内存 fixture 测试覆盖检测关联、类别映射、置信度、漏检、模型边界、事件合并、视频跨度合法性及长序列状态上限。它们不是大鼠行为识别准确率或 24h 板端稳定性证明。

实际主业务每路 AI worker 已接入上述软件层，并在 `--events` 文件写入完成事件；同一文件的普通推理统计没有 event_id。录像完成后使用 `tools/associate_events.py --events <events.jsonl> --index <recording-index.jsonl> --output <new-output.jsonl> --recording-dir <recordings>` 补充真实分段引用。工具检查 finalized:true、Camera/sequence/source timestamp 和可选文件存在性；没有完整索引、未知源时钟或跨会话序号冲突时保留 unmapped/partial，避免填入未经证明的录像路径。
