# 阶段 8：行为时间事件与录像关联

已读取阶段 5、6、7 报告，复用真实检测结果和新建录像 packet/finalize 索引。主业务每路 AI worker 现在执行 `DetectionResult → RatTracker → classify_behavior → EventManager`，shutdown 时输出最终事件；普通推理统计与完成事件一并写入 `--events` JSONL，完整事件通过 event_id 区分。

软件实现：
- DetectionResult 的 Camera/原始 sequence/source timestamp/monotonic timestamp/model id/version 与 bbox/class/confidence 分开保留。
- Camera-local IoU tracker 有 max_tracks 和失联期限；模型版本变化重置相应 Camera 的轨迹。track_id 不代表真实个体身份。rat_id 仅接受模型明确提供的可选身份，当前主业务未填。
- 行为必须经 sidecar 显式 behavior_classes 映射；rat-only 模型和 Demo 不生成实验行为。状态机按 Camera/track 隔离，使用最少观测次数、最短持续时间、置信度阈值、短漏检 grace 和模型/类别变化处理开始/结束。事件的结束时间为最后有效观测，时长使用 monotonic 域。
- 两路 Camera ID 固定0/1，状态数、模型标识/行为名称长度有界；乱序/重复帧、过期 watermark 后的结果、NaN、空语义和过多目标拒绝。调用方每路单线程持有 tracker/manager，无共享可变标签表。
- 最终事件输出 event_id、Camera、track、可选 rat_id、行为、源/monotonic 起止、持续时间、置信度、首尾 sequence、model id/version，JSONL/CSV 文本转义。视频引用允许多段 VideoSpan。

离线工具 `tools/associate_events.py`：

```bash
python tools/associate_events.py --events events.jsonl \
  --index recordings/recording-index.jsonl \
  --output associated-events.jsonl --recording-dir recordings
```

实际索引文件名以录像程序输出为准。工具为每个事件流式扫描给定索引（O(E×P)），不把24小时逐包日志一次载入内存；每事件最多10000个跨度。匹配同 Camera/源 sequence/源 timestamp，只将 `finalized:true` 分段列入 video_spans；可选核验 MP4 文件存在。首尾源时间必须精确吻合；缺帧、失败分段、未 finalize、跨会话重复序号、时间冲突或零/未知源时钟不会伪造完整关联。输出 mapped/partial/unmapped、缺帧数量和未完成分段，原日志不覆盖，输出采用独占创建。

使用同一采集会话的事件和索引；索引不提供跨重启统一时钟或动物身份。工具保证索引证据匹配，不代表已经用 ffprobe 验证 MP4 每帧可解码。混合 JSONL 中没有 event_id 的普通统计被跳过。跨分段事件保留多个实际跨度，不构造推测文件名。

真实测试：Python `python -m unittest discover -s tests -p test_associate_events.py -v` 已运行，5/5通过；包括多分段、缺帧/失败 finalize、错误 Camera/时钟、真实文件缺失、保护既有输出、重复会话序号和未知源时钟。此前主代理报告 behavior_event_test/model_contract_test 通过；本阶段最后自审修改后的统一 C++ 重建结果由阶段9记录，不能沿用旧测试作为最后版本的完成证明。

剩余边界：不存在已验证大鼠行为模型、训练/标注数据或识别精度。IoU 遮挡/交叉 ID 稳定性、持续行为模型适用性、实际录像可解码关联和双路24h/72h稳定性为 `BLOCKED_HARDWARE`。事件发布会等待推理更新、超时 tick 或 shutdown；主业务无独立行为模型时不会输出结果。
