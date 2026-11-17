# 阶段 5：AI 模型契约与行为分层

已读取阶段 3/4 报告及现有 `IInferenceBackend`、`rknn_lite`、后处理和双路异步队列。复用独立 Software ISP 的 RGB backend 接口，Demo 仍明确标记 SIMULATED；主业务不连接 Demo，也不将它的检测转为行为事件。主机 RKNN 请求失败并给出 `RKNN_BACKEND_UNAVAILABLE`。

新增 `ai/model_contract.h/.cpp`：模型 sidecar 严格解析 model_id/version、输入尺寸、RGB/BGR、NHWC/NCHW、UINT8/INT8/FLOAT32、stretch/letterbox、mean/std/multiplier、量化参数、YUV matrix/range、padding、输出 schema、class_names 和显式 behavior_classes。缺键、重复键、未知键、非法数字均失败。无 metadata 禁止猜测。

CPU 参考预处理复用 CameraFrame 的布局验证、lease 与实际 plane/stride/offset/bytesused，支持 NV12/NV12M、裁剪、最近邻 resize/letterbox、通道顺序、张量排布、归一化和量化。主业务已将契约预处理张量传给 rknn_lite，不依赖未知的 RGA 默认色彩矩阵生成模型输入。它不是 RGA 实现，也未作为硬件性能结论。模型 RGB 参考结果及布局由主机测试验证。

现有 `rknn_lite` 仍是特定 YOLOv5 三头 adapter，非通用 RKNN runner。新增必需模型契约、输入 rank/batch/dim/channel 及输出 rank/layout/INT8/affine scale/head shape 检查；只接受显式声明且匹配当前实现的模型，拒绝误用其他输出格式。后处理使用每模型配置的 class_names，不再依赖固定相对 labels 文件或跨 Camera 共享可变标签表；检测保留 class_id。

新增软件层 `behavior/event_manager.h/.cpp`：元数据完整的 DetectionResult → camera-local IoU Tracking → 显式 behavior_classes 映射 → 时间状态机。track_id 不等于动物个体 rat_id。单纯 rat detector 的空 behavior_classes 不输出行为事件；连续行为模型的有效性、实验行为定义、训练数据、精度均需另行验证。

测试：`model_contract_test` 覆盖带 stride 的 NV12M、对齐 UV offset 的 NV12、短 payload、crop、letterbox、RGB/BGR/NCHW、FLOAT32/INT8 和 sidecar 错误；`behavior_event_test` 覆盖事件时序。实际构建/测试结果统一写入 `08_TEST_RESULTS.md`，不把尚未执行的检查写成通过。

板端状态 `BLOCKED_HARDWARE`：无已提供的大鼠模型、SDK/开发板/真实 Camera；RKNN ABI、RGA conversion 色彩矩阵、模型输出定义、NPU context 资源和推理吞吐待板端确认。主业务接线状态见后续阶段 6/7 及最终状态报告。
