# RKNN / YOLO / NPU 知识体系

## 1. 模型生命周期

RKNN 应用通常经历：读取 `.rknn` → `rknn_init` → 查询输入输出属性 → 设置 input → `rknn_run` → `rknn_outputs_get` → 后处理 → `rknn_outputs_release` → `rknn_destroy`。

项目实现位于 `include/rknnPool.hpp:63-210,223-307`：构造函数加载模型、初始化 context、设置 core mask、查询 SDK/输入/输出属性；`interf()` 做 BGR→RGB、resize、输入提交、运行、输出获取和释放。

当前状态：有源码调用，但没有 RKNN runtime、模型、NPU driver 或目标构建证据。

## 2. 输入 Tensor

Tensor 属性包括 dims、format、type、size、quantization scale/zp。模型是 NCHW 还是 NHWC 会影响内存布局，不能只看尺寸。

当前代码在 `rknnPool.hpp:191-209` 读取 NCHW/NHWC 尺寸后，统一设置 `inputs[0].fmt = RKNN_TENSOR_NHWC`。这是待修复的 P1 问题：如果模型实际是 NCHW，输入布局可能不正确。

正确面试回答：先 `rknn_query` 读取模型契约，再决定转换或拒绝不支持的模型；不能硬编码“所有模型都是 NHWC”。

## 3. INT8 量化

INT8 输出通常需要 `real = (q - zp) * scale` 反量化；输入也可能有量化或由 runtime 做转换。`output_attrs[i].scale/zp` 在 `rknnPool.hpp:271-277` 收集并传给 `post_process`。

扩展学习：per-tensor/per-channel quantization、DFP、zero point、symmetric/asymmetric、量化误差和 calibration。当前仓库没有模型准确率或量化误差实验。

## 4. YOLO 后处理

YOLO 典型流程：三个 stride 输出 → 解码 anchor/grid → 置信度阈值 → NMS → 坐标映射回原图 → OSD。`lib/postprocess.cpp:307-329` 处理 stride 8/16/32，`:339-352` 排序并按类别做 NMS，`:358-392` 生成 `detect_result_group_t`。

当前源码有两个需要面试时主动承认的正确性风险：

- `postprocess.cpp:114-149` 的 NMS 内层候选检查使用 `classIds[i]`，需核对是否应使用 `classIds[j]`；
- `:373-387` 取 confidence 使用排序位置 `objProbs[i]`，而当前 box index 是 `n=indexArray[i]`，应验证是否应为 `objProbs[n]`。

不要把“代码里有 YOLO 后处理”说成模型效果已经验证。

## 5. 坐标映射与 OSD

输入图像可能经过 resize/letterbox，检测框必须反向映射到原图和拼接画布。`lib/dual_camera_pipeline.cpp:325-360` 用 `CompositeProcessor::map_box/clip_box` 映射到 camera view，再用 OpenCV 绘制矩形和标签。

`lib/composite_processor.cpp:108-123` 提供 box mapping/clipping；实际坐标是否和模型输入、拼接布局一致，需要合成图测试验证。

## 6. 两个 NPU Context

双路目标需要两个独立 context，分别对应 camera0/camera1 的模型、帧序号和结果。`DualCameraPipeline::Impl` 中 `models[2]`、`ai_snapshot[2]` 和 `ai_threads[2]` 位于 `lib/dual_camera_pipeline.cpp:190-211,286-317`。

当前问题：`dual_camera_pipeline.cpp:435` 两个模型都传 `n=0`，导致 `rknnPool.hpp:89-97` 都设置 core 0；CLI `--npu-core0/1` 只打印警告。面试中应说“两个 context 的软件隔离已设计，核绑定配置尚未正确生效”。

## 7. 推理时序

AI 队列中保存 `CameraFrame`、sequence 和 capture timestamp；`ai_loop` 先复制 NV12、转换 BGR、释放源 frame，再调用模型。这样慢推理不会继续占用 V4L2 mmap，但结果可能滞后。

`compose_pair` 根据 snapshot 的 sequence/timestamp 判断结果是否可画（`dual_camera_pipeline.cpp:336-358`）。Strict 模式等待指定 sequence，Async 模式允许较旧但 timestamp 不晚于当前帧的结果。

当前限制：`models[id]->interf()` 不可取消；NPU 卡死时 join 可能无界。

## 8. 常见追问

- 为什么不用一个模型处理两路？答：软件上可以共享模型，但两路独立 context 更易隔离队列和结果；当前配置和性能需目标板实测。
- 为什么 AI 不直接读 DMA-BUF？答：当前采用 copy/RGA 保守路径，尚未证明 RKNN DMA-BUF import/fence 契约。
- 为什么 AI 可以丢帧？答：AI 是辅助事件分支，录像优先；队列满时 drop-oldest，但要单独统计并接受结果延迟。
- 如何防止旧框画到新帧？答：sequence/timestamp 校验；但必须验证时钟域和结果策略。
- NPU core 绑定是否完成？答：wrapper 有 API，但当前 dual pipeline 没把 CLI core 参数传入，仍是待修复项。

## 9. 扩展学习

RKNN zero-copy、rknn_tensor_mem、DMA-BUF fd import、multi-input/multi-output、dynamic shape、NPU profiling、模型量化校准和多核调度属于理论/目标板学习，不代表当前项目已验证。
