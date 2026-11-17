# RKNN 接入与模型 sidecar

主业务继续使用现有 `rknn_lite` adapter，独立 Software ISP 的 IInferenceBackend/RKNN factory 明确不可用；两者不混淆。构建必须使用 BSP 配套的真实 rknn_api.h/runtime library，不能把仓库旧头文件当 ABI 证明。打开 ENABLE_RKNN 但依赖缺失时构建配置失败。

主业务 `--model-contract <file>` 与模型路径共同配置。sidecar 是严格的 key=value 文件，必需字段如下；值必须由模型导出配置和采集色彩信息确认，下面仅为格式示例，不能直接认定适合未知模型：

```ini
model_id=REPLACE_WITH_ACTUAL_MODEL
model_version=REPLACE_WITH_ACTUAL_VERSION
input_width=640
input_height=640
layout=NHWC
channel_order=RGB
tensor_type=UINT8
resize=stretch
yuv_matrix=BT601
yuv_range=limited
multiplier=1
mean=0,0,0
stddev=1,1,1
quant_scale=1
quant_zero_point=0
padding=114
output_schema=yolov5_int8_three_head
class_names=rat
behavior_classes=
```

目前硬件 adapter 仅支持 RGB UINT8 NHWC、stretch、无 CPU 归一化、YOLOv5 固定 anchors 的三个 NCHW affine-INT8 输出头。输入 shape/batch/channel、三头 shape、quant scale 与 SDK 查询值必须匹配。其他模型，包括 YOLOv8、分类器、时序模型、不同 anchors，需要对应的 preprocess/postprocess adapter；不能靠修改类别数兼容。SDK 的内部量化转换与此处原始 RGB uint8 输入契约分开验证。

通用 CPU `preprocess_nv12` 参考实现支持更多张量类型及 letterbox，仅说明软件能力；不表示硬件 runner 已支持这些组合。nearest-neighbor resize 必须匹配模型训练/export 约定，否则需更换为对应插值并重测。RGA 色彩范围/矩阵必须与 sidecar、V4L2 协商值和模型预处理一致。

集成顺序：记录 BSP/runtime 版本 → 校验 model 文件 hash/版本/输出 schema → 执行 tensor attr 查询 → 在相同图片上比较参考张量与部署张量 → 对比后处理框/标签 → 测量时延/队列丢弃 → 双路共享 NPU 资源和 24h/72h 长稳。无真实模型不报告精度，未上板不报告 NPU 性能。状态为 `BLOCKED_HARDWARE`。

来源风险：已有 rknnPool.hpp 与 postprocess.h 保留其原始限制性版权声明；应由项目所有者确认其再发布/派生授权。本轮未改写许可证，也未从参考仓库复制源码或 SDK 二进制。
