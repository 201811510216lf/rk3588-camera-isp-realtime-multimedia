# RK3588 ISP采集测试报告

本轮基线main=1119530，最初工作树干净。当前任务聚焦真实V4L2采集及必要的原Pipeline集成，不沿用上一轮26组结果充当新源码验证。

## 验证范围

- Linux v4l2_capture_test链接正式V4L2IspCamera实现，通过测试专用syscall wrapping覆盖G_FMT协商、NV12/NV12M平面/stride/data_offset/bytesused、QBUF最终lease、失败清理和停止；不提供产品Mock输入，不证明真实硬件。
- 现有camera_queue/FramePairer测试增加双消费者关闭与owner/lease寿命；CLI/配置/readiness测试覆盖每路配置继承、显式冲突和协商前后边界。
- isp_capture_cli_test调用真实构建工具检查参数负例、新目录保护和缺失配置；没有采集硬件数据。
- 全量测试包含保留的Software ISP、模型/行为/输出与既有RkAiq测试；不新增Fake Backend。

## 本轮实际结果

| 验证 | 实际结果 |
|---|---|
| Windows/MSVC Debug clean build | 26/26通过，5.01秒 |
| Windows/MSVC ASan clean build | 26/26通过，25.86秒，无ASan报告 |
| 新isp_capture源 / V4L2非Linux分支 | MSVC /W4独立编译通过，不等于Linux采集实现编译 |
| Linux V4L2 / syscall测试 / 工具CLI | 待本轮CI执行；Linux比Windows多两组采集专用测试 |
| 真SDK / 厂商myDemo / 板端 | BLOCKED_HARDWARE |

单个代理的隔离测试仅为开发中证据，不替代上述整体验证。最终Linux结果会在实际完成后追加。

## 硬件验收

真实RKAIQ SDK编译：BLOCKED_HARDWARE（本仓库/构建未配置目标头、库、API port）。厂商myDemo SDK编译：BLOCKED_HARDWARE。真实Sensor、IQ/AE/AWB、NV12图像、双路同时采集、RKNN模型、MPP/FFmpeg输出与长稳：全部待板端验证；没有实测数字或伪造图像。

未来必须按IspServer/RkAiq → Sensor → media graph → NV12/NV12M → 单路抓帧 → 查看NV12 → 双路采集 → 原Pipeline → RKNN → MPP H.264顺序验收。命令与证据见[采集说明](RK3588_ISP_CAPTURE.md)和[双路说明](RK3588_DUAL_CAMERA.md)。
