# 第五轮 IMX415 / RK3588 严格代码审查报告

审查日期：2026-11-13（Asia/Shanghai）  
审查范围：当前工作树 `demo_multhread_decode_infer_mulmodel` 及其 Git 历史  
审查约束：本轮只读源码、Git、静态检查、主机测试和构建配置；本报告是本轮唯一新增交付物，未修改业务源码、驱动、DTS 或现有配置。

## 1. 总体结论

当前工程是一个有真实用户态实现的双路 V4L2 NV12/NV12M → 软件配帧 → RKNN → RGA/OpenCV → 单 MPP H.264 → 多 sink 结构，但不是已经完成的 IMX415/RK3588 ISP bring-up 工程。

- **IMX415 Sensor Driver：NOT_IMPLEMENTED / STUB_ONLY（BLOCKED_BY_HARDWARE_INFO）**。仓库没有 kernel tree、`drivers/media/i2c/imx415.c`、mode table、寄存器初始化表或 I2C probe 证据。
- **DTS/CSI/DPHY/RKCIF/RKISP：NOT_IMPLEMENTED（BLOCKED_BY_HARDWARE_INFO）**。只有占位 overlay 模板和只读 validator，没有目标板 DTS、DTB 或 media graph 日志。
- **RKISP 实际出图：NOT_TESTED**。`lib/v4l2_isp_camera.cpp` 只消费已经协商出的 NV12/NV12M video node，不能证明 RAW Bayer 已经进入 RKISP。
- **rkAIQ：NOT_IMPLEMENTED（BLOCKED_BY_SDK）**。`AiqController` 只发现路径/进程并 fail-closed；没有真实 rkAIQ headers、库、context、IQ load 或 AE/AWB 调用。
- **IQ：PATH/HASH INVENTORY ONLY**。`scripts/iq_manifest.py` 能做只读路径和 SHA-256 清单，但没有真实 IQ 文件、schema、SDK 版本或成功加载结果。
- **AE/AWB：NOT_IMPLEMENTED**。分析器只做离线 RAW/NV12 统计，并明确输出 `NOT_MEASURED`/`NOT_INFERRED`；这不构成 AE/AWB 算法或 ISP 控制闭环。
- **RAW10/RAW12：SOURCE_IMPLEMENTED + HOST_TESTED**。`scripts/isp_quality_analyzer.py` 支持 packed RAW10/RAW12 解包和 Bayer 指标，但没有板端 RAW 采集证据。
- **双路 ISP/硬件同步：NOT_TESTED / BLOCKED_BY_HARDWARE**。双路用户态结构和软件时间戳配帧存在；硬件 FSIN/Trigger、两路独立 DPHY/RKCIF/RKISP 未证明。
- **AI/MPP/RTMP/MP4：SOURCE_PRESENT，TARGET/BOARD NOT_TESTED**。主链路代码存在，但 sink 错误会被忽略，慢 sink 会阻塞编码线程，不能把 README 的结构描述当成运行成功。

总体评级：`IMPLEMENTED_AWAITING_HARDWARE`。P0 已证实问题为 0；存在若干 P1 源码级主链路缺陷和一个 P0 级“MPP completion 语义未验证”风险项，后者不能在没有目标 BSP 证据时误报为已修复或已失败。

## 2. Git 与仓库审计

### Git 状态

本次实际执行：

```text
git status --short
 git log -10 --oneline
 git diff --stat
 git diff --check
```

观察结果：

- HEAD：`6085dab Converge project to single MIPI camera pipeline`。
- 历史提交包含 `9bd2aaa Implement RK3588 Camera/ISP pipeline`、`7b51ff8 Redact camera credentials from public source`、`6c51a51 Remove obsolete artifacts and unused legacy scaffolding`。
- 工作区存在大量未提交修改和 untracked 文件；新增 `docs/isp/`、`scripts/isp_quality_analyzer.py`、`scripts/iq_manifest.py`、`tools/isp_debug_tool.cpp`、测试及报告不在 `git diff --stat` 的 tracked 统计中。
- `git diff --check` 退出 0，仅报告 Windows 换行格式提示，没有 whitespace error。

### 仓库结构

当前存在：

- 双路用户态：`main.cpp`、`lib/dual_camera_pipeline.cpp`、`lib/frame_pairer.cpp`、`lib/v4l2_isp_camera.cpp`。
- AI/图像/编码：`include/rknnPool.hpp`、`lib/postprocess.cpp`、`lib/mpp_encoder.cpp`、`lib/composite_processor.cpp`、`lib/encoded_packet_fanout.cpp`。
- ISP 适配准备：`lib/media_graph_validator.cpp`、`lib/aiq_controller.cpp`、`scripts/isp_probe.sh`、`docs/isp/`。
- 测试：5 个 C++ 测试目标源码、两个 Python analyzer 测试套件和结构检查。
- 没有 `.dts/.dtsi`、Linux kernel checkout、Sensor subdev 驱动、rkAIQ SDK headers/libs、IQ 二进制或板端日志。

### 文档冲突

1. `README.md:3` 以“当前主链路是：两路真实 Sensor → CSI/RKCIF/RKISP/rkAIQ”开头，容易被理解成两颗 IMX415 已接入；但 `docs/isp/13_ISP_IMPLEMENTATION_STATUS.md:5-16`、`docs/FINAL_ACCEPTANCE_REVIEW.md:18-20` 明确说明没有板卡/BSP/Sensor/IQ、未做板端验证。该冲突列为 **P2-文档真实性**。
2. `CODEX_CAMERA_ISP_EXECUTION_PLAN.md:5,13-14,37` 仍描述“最终只交付单路 MIPI Camera”，并禁止 RTMP/四路扩展；当前 `README.md` 和 `DualCameraPipeline` 已经是双路并保留 RTMP/MP4 sink。该计划与当前最新双路目标不一致，列为 **P2-架构文档陈旧**。
3. `tests/validate_project.py:71-73` 检查双路入口，但同一工程计划仍检查单路收敛；结构检查通过不代表文档和产品目标一致。

## 3. 功能真实性矩阵

| 功能 | 状态 | 证据 | 审查结论 |
|---|---|---|---|
| IMX415 kernel Sensor driver | `NOT_IMPLEMENTED / STUB_ONLY / BLOCKED_BY_HARDWARE_INFO` | 无 kernel/BSP；`docs/isp/02...` | 不是 REUSED/MODIFIED/NEW_SENSOR_DRIVER |
| I2C probe/chip ID/power/reset/xclk | `NOT_TESTED` | 无板端 dmesg/I2C | 无法确认地址、GPIO、供电和时钟 |
| RAW10/RAW12 mode table | `NOT_IMPLEMENTED` | 无真实 mode table | 公开 datasheet 不能替代模组 BSP 表 |
| DTS/DTB | `STUB_ONLY / BLOCKED_BY_HARDWARE_INFO` | `docs/isp/patches/imx415-rk3588-overlay-template.dts` | 占位模板不可直接应用 |
| CSI/DPHY/RKCIF/RKISP graph | `NOT_TESTED` | `lib/media_graph_validator.cpp:106-153` 仅做文本检查 | 没有实体/link 解析或板端输出 |
| V4L2 NV12/NV12M capture | `SOURCE_IMPLEMENTED` | `lib/v4l2_isp_camera.cpp:203-482,484-697` | 用户态源码存在，C++ 未构建 |
| RKISP RAW→NV12 | `NOT_TESTED` | 无 `/dev/media*`、RAW/NV12 实采 | V4L2 node 不是 Sensor/ISP 证据 |
| rkAIQ context/3A | `NOT_IMPLEMENTED / BLOCKED_BY_SDK` | `lib/aiq_controller.cpp:83-124` | 所有控制函数返回 `Unsupported` |
| IQ 文件加载 | `NOT_TESTED` | `scripts/iq_manifest.py` 仅路径/hash | 没有 schema/SDK/load result |
| AE/AWB 控制/回读 | `NOT_IMPLEMENTED` | `lib/aiq_controller.cpp:102-124` | 无真实 API、统计或回读 |
| RAW10/RAW12 离线分析 | `SOURCE_IMPLEMENTED / HOST_TESTED` | `scripts/isp_quality_analyzer.py:27-58,212-292` | 只分析样本，不实现 ISP |
| 双路用户态 capture/配帧 | `SOURCE_IMPLEMENTED` | `lib/dual_camera_pipeline.cpp:238-284`、`lib/frame_pairer.cpp` | 未在 Linux/RK3588 构建运行 |
| 软件时间戳配帧 | `PARTIAL / SOFTWARE_TESTED` | `lib/frame_pairer.cpp:61-220` | Unknown clock domain 会拒绝配对 |
| 硬件同步曝光 | `NOT_IMPLEMENTED / BLOCKED_BY_HARDWARE` | `lib/dual_camera_pipeline.cpp:392` | hardware mode 只 fail-fast |
| 两个 RKNN context | `SOURCE_PRESENT / TARGET NOT_TESTED` | `lib/dual_camera_pipeline.cpp:435` | 模型路径可独立，但 core 参数未生效 |
| RGA/CPU 转换和拼接 | `SOURCE_PRESENT / TARGET NOT_TESTED` | `lib/dual_camera_pipeline.cpp:319-364` | 无目标 SDK/实测 |
| MPP H.264 | `SOURCE_PRESENT / TARGET NOT_TESTED` | `lib/mpp_encoder.cpp:322-437` | completion/flush 语义未由 BSP 证明 |
| RTMP/MP4 | `SOURCE_PRESENT / BOARD NOT_TESTED` | `lib/dual_camera_pipeline.cpp:122-174,411-426` | ffmpeg/server/player 未验证，sink 错误未传播 |

## 4. Sensor Driver 审查

仓库中不存在 Sensor driver 源码。`docs/isp/02_IMX415_SENSOR_DRIVER.md` 只列出应审查的 `probe/remove`、`set_fmt/get_fmt`、`s_stream`、controls 和 runtime PM 路径，没有实际函数实现。

因此分类为：

- `STUB_ONLY`：适配说明、函数清单和上游参考存在。
- `NOT_IMPLEMENTED`：没有 `drivers/media/i2c/imx415.c` 或 vendor BSP diff。
- `BLOCKED_BY_HARDWARE_INFO`：缺具体板、模组 mode table、I2C/GPIO/regulator/xclk 和 BSP commit。

不能把 `lib/v4l2_isp_camera.cpp` 误称为 Sensor driver；该文件从 V4L2 输出节点读取 NV12/NV12M，关键路径为 `VIDIOC_QUERYCAP/S_FMT/REQBUFS/QUERYBUF/EXPBUF/QBUF/DQBUF/STREAMON/STREAMOFF`。

## 5. DTS / CSI / DPHY / RKCIF / RKISP 审查

没有真实 DTS/DTSI、DTB 或板级原理图。`docs/isp/patches/imx415-rk3588-overlay-template.dts` 使用 `__BOARD_SPECIFIC__` 占位符，明确不能编译/应用到未知板。

`lib/media_graph_validator.cpp:106-153` 的检查内容是：

- `media-ctl -p` 输出非空；
- 两个 `v4l2-ctl` 输出非空；
- 文本中出现 NV12/NV12M、目标尺寸和 FPS；
- 两个 video node 不同。

它没有解析 sensor entity、CSI/DPHY、RKCIF、RKISP entity 之间的 link、endpoint、lane 或 remote-endpoint。因此即使返回 `ok()`，也不能证明 RKISP media graph 正确。这是 **P2-验证覆盖不足**，不是硬件错误证据。

## 6. RKISP / rkAIQ 审查

### RKISP 数据路径

`lib/v4l2_isp_camera.cpp` 只对已经存在的 V4L2 capture node 做格式协商和 buffer 管理；仓库没有 RAW Bayer node、ISP subdev 控制、RKISP pipeline setup 或设备树 endpoint。`scripts/isp_quality_analyzer.py` 的 RAW 解包和 Bayer 统计是离线主机工具，不是 ISP 数据通路。

### rkAIQ

`lib/aiq_controller.cpp:83-99` 的 `query_status()`：

- 扫描 `/proc` 进程名；
- 递归扫描 IQ/runtime 路径中是否出现 `rkaiq`/`rkisp` 字符串；
- 将匹配结果标为 `UNVERIFIED`；
- 固定 `control_backend_available=false`。

`set_ae_auto/set_exposure_us/set_gain/set_awb_auto` 位于 `:102-124`，均返回 `Unsupported`。没有 `rk_aiq_uapi_*`、AIQ context 创建、IQ load、3A stats、start/stop、exposure/gain feedback 或双 context。

这符合 fail-closed 设计，但不能认定为 rkAIQ 集成。状态应为 `BLOCKED_BY_SDK`，不是 `IMPLEMENTED`。

## 7. IQ / AE / AWB / RAW 图像质量审查

- `scripts/iq_manifest.py:24-45` 只遍历文件、计算 SHA-256、做 Sensor 文件名匹配；`schema_compatibility=BOARD_LOADER_REQUIRED`、`sdk_version=NOT_IDENTIFIED` 是诚实的占位状态。
- `scripts/isp_quality_analyzer.py:27-58` 实现 RAW10/RAW12 packed 解包；`:212-240` 做 Bayer 通道和黑电平统计；`:243-259` 做 NV12 Y 平面统计；`:261-292` 做帧间亮度/通道比例趋势。
- 分析器明确输出 `NOT_MEASURED`、`NOT_INFERRED` 和 `IMAGE_ONLY_HEURISTIC`。它不能证明 AE/AWB 控制、demosaic、3DNR、gamma、CCM 或硬件 ISP tuning。
- `docs/isp/06/07/08` 把实验设计和指标边界写清楚，但没有真实样本、曝光/增益回读、光源条件、色温或收敛曲线。

结论：RAW 分析工具为 `SOURCE_IMPLEMENTED + HOST_TESTED`；AE/AWB/IQ tuning 为 `NOT_IMPLEMENTED/NOT_TESTED`。

## 8. 双路同步审查

`FramePairer` 使用 V4L2 timestamp、sequence、clock domain 和有界队列：

- `lib/frame_pairer.cpp:61-67` 拒绝 Unknown clock domain；
- `:88-160` 搜索容差内最优匹配；
- `:162-194` 对超时或 HoldLast 生成结果；
- `lib/dual_camera_pipeline.cpp:392-393` 对 hardware mode fail-fast。

当前只能证明：

1. 双路用户态结构存在；
2. 软件时间戳配帧代码存在；
3. 配帧队列和超时策略有界；
4. C++ 测试源码覆盖已知/未知 clock domain。

不能证明：

- 两颗 IMX415 已 probe；
- 两路 CSI/DPHY/RKCIF/RKISP 同时工作；
- FSIN/XVS/XHS/master-slave 已配置；
- 同步曝光误差已测量。

附加风险：未知 clock domain 会使所有候选被拒绝，当前没有板端 fallback 或明确失败统计提升到 pipeline error；应在真实 V4L2 timestamp flags 上验证。

## 9. AI / RGA / MPP / RTMP / MP4 审查

### RKNN

`include/rknnPool.hpp:63-210` 确实调用 `rknn_init/rknn_set_core_mask/rknn_query`，并拒绝 `n_input != 1`、`n_output < 3`。但：

- `:191-209` 根据模型属性读取 NCHW/NHWC 尺寸后，无条件设置 `inputs[0].fmt = RKNN_TENSOR_NHWC`；如果模型实际是 NCHW，输入布局契约可能错误。这是 **P1-001**。
- `lib/dual_camera_pipeline.cpp:435` 对两个模型都传构造参数 `n=0`；`include/rknnPool.hpp:89-97` 因此两个 context 都设置 `RKNN_NPU_CORE_0`。CLI 的 `--npu-core0/--npu-core1` 在 `dual_camera_pipeline.cpp:436` 只打印 warning，实际不生效，列为 **P2-001**。
- 没有真实 `.rknn` 模型、RKNN runtime、NPU driver 或目标编译证据。

`lib/postprocess.cpp:114-149` 的 NMS 内循环使用 `classIds[i]` 检查候选 `j`，应核对 `classIds[j]`；`:373-387` 在排序后的 `indexArray` 中使用 `objProbs[i]`，而不是当前候选 `objProbs[n]`。这会造成类别过滤和置信度与 box 不一致，列为 **P1-002**，仅在模型实际输出进入该路径时触发。

### RGA/拼接

`lib/dual_camera_pipeline.cpp:319-364` 通过 `imcvtcolor/imresize` 和 OpenCV fallback 完成 NV12→BGR、左右拼接、OSD 和 BGR→NV12。源码存在，但没有 DMA-BUF import、cache/fence 或目标 RGA 同步证据。

### MPP

`lib/mpp_encoder.cpp:322-437` 采用 `encode_put_frame` 后轮询 `encode_get_packet`。在 timeout/非零错误时 `:399-417` 将 pending buffer 保留到 `Release()` 销毁 context；这是保守实现，但目标 BSP 没有完成语义证据。该风险为 **P0-NOT_VERIFIED**，不是已证实 P0。

`Encode()` 允许 `yuv_size < expected_yuv_size`（`:332-337`），随后只 `memcpy` 有效前缀（`:350-365`），未初始化尾部可能被送入硬件，列为 **P2-002**。当前 `EncodeNV12` 调用通常传入完整 packed size，因此这是公共 API 契约缺陷。

### RTMP / MP4 / raw sink

`FfmpegPipe::write()` 位于 `lib/dual_camera_pipeline.cpp:147-159`，使用阻塞 `fwrite`；`EncodedPacketDistributor::publish()` 在 `lib/encoded_packet_fanout.cpp:15-49` 同一编码线程顺序调用所有 sink。结果是：

- 慢 RTMP 会阻塞 MP4/raw；
- 慢 MP4 会阻塞 RTMP/编码；
- broken pipe 会关闭、重开并重试，同一 packet 可能出现部分写入后重复写入；
- 没有每 sink 独立队列、超时或线程隔离。

`dual_camera_pipeline.cpp:376` 忽略 `distributor.publish()` 返回值；`:418-426` 只增加 RTMP/MP4 错误计数；raw sink 的 false 也不会设置 `failure`；`:448` 对 raw/events/sync_diag 的 flush/close 不检查。`run()` 最终在 `:451` 主要依据 `p.failure.empty()` 返回，因而磁盘满、ffmpeg 退出、RTMP 断线或 MP4 写失败可能仍返回成功。这是 **P1-003**。

`FfmpegPipe::open_rtmp/open_mp4` 只检查 `popen()` 返回非空（`:125-142`）；shell/ffmpeg 子进程不存在时，`popen` 仍可能成功建立管道，真正失败延迟到写入或 `pclose`。这也是 **P1-003** 的可观测性缺口。

## 10. P0/P1/P2/P3 问题清单

### P0

**已证实 P0：0。**

**P0-NOT_VERIFIED-001：MPP 输入 buffer 完成语义未由目标 BSP 证明**  
证据：`lib/mpp_encoder.cpp:375-417,477-497`。  
风险：timeout/错误后无法证明硬件停止读取；不能在没有 completion/reset 语义时提前复用或释放 buffer。当前代码选择保守地保持到 context destroy，但这只解决了“避免猜测”，没有完成板端验证。  
验收：目标 BSP 上确认 `encode_put_frame/get_packet`、timeout、flush、EOS、packet ownership 和 context destroy 顺序；加入真实或 SDK mock 证据。

### P1

**P1-001：RKNN NCHW 模型被强制标记为 NHWC**  
证据：`include/rknnPool.hpp:191-209`。  
影响：模型输入布局可能错误，推理结果错误或 runtime 拒绝。  
最小修复：按 `input_attrs[0].fmt` 设置 input，或明确只支持 NHWC 并在初始化时拒绝 NCHW。

**P1-002：后处理 NMS/置信度索引错误**  
证据：`lib/postprocess.cpp:114-149,373-387`。  
影响：跨类别 NMS 条件和输出 confidence 可能与 box 不匹配。  
最小修复：核对 `classIds[j]`、`objProbs[n]`，增加合成输出回归测试。

**P1-003：多 sink 写失败不传播，且 sink 同步阻塞**  
证据：`lib/dual_camera_pipeline.cpp:147-159,376,413-426,448-451`；`lib/encoded_packet_fanout.cpp:15-49`。  
影响：RTMP/MP4/raw 失败时 pipeline 可能返回成功；任一 sink 的慢写阻塞其它 sink 和编码。  
最小修复：每 sink 独立有界队列/线程或明确超时；失败设置 pipeline failure；检查 header/packet/final flush/pclose 状态。

**P1-004：V4L2 multi-planar plane contract 允许不一致**  
证据：`lib/v4l2_isp_camera.cpp:365-376,410-416,590-680`。  
影响：`NV12M` 协商为两 plane 时，`QUERYBUF` 返回较少 plane 仍被接受；后续可能把 plane 0 当连续 Y+UV 或错误设置 `data_uv`，导致颜色错误/越界风险。  
最小修复：NV12 强制单 plane、NV12M 强制两 plane；每个 QUERYBUF 的 plane count 必须与协商值相等。

**P1-005：AI/RKNN 调用不可取消，stop/join 无界**  
证据：`lib/dual_camera_pipeline.cpp:286-317,444-449`。  
影响：RKNN/NPU/driver 卡死时析构和 stop 可能无限等待，V4L2 STREAMOFF/资源清理被拖住。  
最小修复：目标 SDK 可支持时使用 timeout/fence；否则把不可取消边界隔离到可终止 worker，并记录明确 watchdog 状态。

**P1-006：IMX415/RKISP/rkAIQ 主链路缺失**  
证据：仓库无 Sensor driver、DTS/DTB、BSP、IQ、rkAIQ headers/libs；`docs/isp/13...:5-16`。  
影响：项目不能声称真实 Sensor bring-up、ISP 出图、AE/AWB 或 IQ tuning。  
状态：`BLOCKED_BY_HARDWARE_INFO` + `BLOCKED_BY_SDK`，不是“缺少测试就算完成”。

### P2

**P2-001：`--npu-core0/--npu-core1` 配置不生效**  
证据：`lib/dual_camera_pipeline.cpp:435-436`、`include/rknnPool.hpp:89-97`。  
影响：两个 context 实际都绑定 core 0，CLI 与运行行为不一致。  

**P2-002：短 YUV 输入会把未初始化尾部交给 MPP**  
证据：`lib/mpp_encoder.cpp:332-365`。  
影响：公共 API 数据契约不完整；应精确拒绝或清零。

**P2-003：events/sync_diag 打开和 flush 失败不可观测**  
证据：`lib/dual_camera_pipeline.cpp:311-315,437-440,448`。  
影响：诊断文件缺失时运行仍可能成功，验收证据不完整。

**P2-004：media graph validator 只做文本存在性检查**  
证据：`lib/media_graph_validator.cpp:106-153`。  
影响：不能证明 sensor→CSI/DPHY→RKCIF→RKISP link、endpoint 或 lane 正确。

**P2-005：`pair_frames` 统计被赋为 encoded_frames**  
证据：`lib/dual_camera_pipeline.cpp:378`。  
影响：配对数和编码数混淆，无法判断 pair drop 与 encode drop。

**P2-006：异常路径依赖析构而非统一 cleanup**  
证据：`lib/dual_camera_pipeline.cpp:401-435`。  
影响：中途返回时相机、sink、encoder 可能保持到外层对象析构；调用方若保留对象，失败后的资源状态不立即收敛。

**P2-007：README/执行计划/ISP 状态文档不一致**  
证据：`README.md:3`、`CODEX_CAMERA_ISP_EXECUTION_PLAN.md:5,13,37`、`docs/isp/13...:5-16`。  
影响：面试官或使用者可能把目标架构误读为已验证硬件链路。

**P2-008：C++/BSP 构建未执行，Python 结构检查覆盖不足**  
证据：本轮 CMake/CTest 命令失败；`tests/validate_project.py:55-116` 主要做文件/token 检查。  
影响：当前通过结果不能证明 V4L2、MPP、RGA、RKNN 或 `isp_debug_tool` 可编译。

### P3

- 两套 Python analyzer 测试存在功能重叠：`tests/isp_quality_analyzer_test.py` 与 `tests/test_isp_quality_analyzer.py`。
- `docs/isp/` 已较完整，但部分文件使用“SOURCE_IMPLEMENTED”作为文档/工具存在状态，读者仍需结合 `BOARD_VERIFIED=NOT_TESTED` 解读。
- 工作区新增文件尚未提交；审查报告和代码版本的可追溯性依赖当前未提交工作树。

## 11. 构建和测试结果

### 实际执行命令

| 命令 | 结果 |
|---|---|
| `git status --short` | 退出 0；大量 tracked 修改和 untracked 文件 |
| `git log -10 --oneline` | 退出 0；HEAD `6085dab` |
| `git diff --stat` | 退出 0；只统计 tracked 修改 |
| `git diff --check` | 退出 0；仅换行格式 warning |
| `cmake -S . -B build-host -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON -DBUILD_BSP_TOOLS=ON` | 失败：PowerShell 提示 `cmake` 不是命令 |
| `cmake --build build-host -j` | 失败：PowerShell 提示 `cmake` 不是命令 |
| `ctest --test-dir build-host --output-on-failure` | 失败：PowerShell 提示 `ctest` 不是命令 |
| `python -m compileall -q scripts tests` | 退出 0 |
| `python tests/validate_project.py` | 退出 0：`validate_project: PASS (36 required files)` |
| `python -m unittest discover -s tests -p 'test_isp_quality_analyzer.py'` | 6/6 通过 |
| `python -m unittest discover -s tests -p 'isp_quality_analyzer_test.py'` | 3/3 通过 |

不能把 Python 通过结果替代 C++/BSP 构建结果。当前没有可记录的 `HOST_BUILT`、`TARGET_BUILT` 或 `BOARD_VERIFIED` 证据。

## 12. 岗位能力评价

### Camera Sensor Driver

- 真实能力：能列出 IMX415 适配所需的 V4L2 subdev、mode table、controls、runtime PM、DTS endpoint 和错误回滚边界。
- 有源码但未验证：无 Sensor driver 源码；只有用户态 V4L2 采集和审计文档。
- 完全缺失：I2C probe、chip ID、寄存器初始化、stream on/off、真实 mode table。
- 可讲难点：为何不能从 Sony 芯片规格推导模组 lane/I2C/GPIO/IQ；为何必须以 BSP/原理图/板端 graph 闭环。

### Linux Camera / BSP

- 真实能力：V4L2 capability/format/REQBUFS/MMAP/EXPBUF/QBUF/DQBUF/poll、NV12/NV12M payload 边界和 lease 生命周期。
- 有源码但未验证：media graph validator、只读探测脚本、DTS 占位模板。
- 完全缺失：真实 DTS/DTB、CSI/DPHY/RKCIF/RKISP graph、Sensor probe。
- 面试质疑点：`QUERYBUF` plane 契约、未知 timestamp domain、实际 video node 是否为 RKISP 输出。

### ISP / rkAIQ / IQ Tuning

- 真实能力：知道需要什么证据和实验指标；有 RAW10/RAW12 离线分析器。
- 有源码但未验证：IQ 路径/hash 清单、AE/AWB 实验模板、fail-closed debug CLI。
- 完全缺失：真实 rkAIQ API、context、IQ load、3A statistics、曝光/gain/AWB 回读和硬件 tuning。
- 面试结论：不能可信地声称做过 Sensor Bring-up、rkAIQ 调用或 AE/AWB 调试；只能说完成了软件接入准备和验证边界设计。

### Linux Multimedia / Embedded C++

- 真实能力：双路有界队列、FramePairer、shared lease、RGA/OpenCV、RKNN wrapper、MPP 编码和多 sink 结构。
- 有源码但未验证：RKNN/MPP/RGA/ffmpeg 调用链、H.264 header/packet 处理。
- 主要风险：sink 同步阻塞、错误不传播、MPP completion 未验证、NCHW 输入布局和后处理索引问题。

## 13. 简历真实性审计

可以写：

- “实现了待板端验证的双路 V4L2 NV12/NV12M 用户态采集、软件时间戳配帧、有界队列和 Buffer lease 生命周期。”
- “搭建了 RGA/OpenCV + RKNN + 单 MPP H.264 + 多 sink 的 Linux 多媒体处理骨架，并加入错误边界和离线 RAW10/RAW12 分析工具。”
- “设计了 RK3588/IMX415 bring-up 的证据矩阵、只读 media graph 探测和 fail-closed rkAIQ 控制接口。”

不能写：

- 已完成 IMX415 Sensor Driver、DTS/CSI/DPHY/RKISP 适配；
- 已真实调用 rkAIQ 或加载 IMX415 IQ；
- 已完成 AE/AWB 调试、同步曝光或双路 ISP；
- 已完成 RTMP/MP4 板端稳定推流/录像；
- 已完成 DMA-BUF 零拷贝、性能优化或 30/60 分钟稳定性验证。

如果我是 ISP 面试官：我会相信候选人理解 bring-up 所需的接口和证据边界，但不会仅凭当前仓库相信候选人已经独立完成过 IMX415 Sensor Bring-up、真实 rkAIQ 调用或 AE/AWB 调试。

最容易被追问的五个问题：

1. 目标板和模组具体型号、I2C 地址、lane/link frequency、XCLK 和 mode table 在哪里？
2. 哪个 `.dts/.dtsi` 把 Sensor 接到了哪个 DPHY/RKCIF/RKISP entity？
3. 使用哪个 rkAIQ SDK/IQ 版本，哪个真实函数创建 context 和提交 AE/AWB？
4. RAW10 是如何从板端采集并证明 packing/Bayer order/black level 的？
5. MPP timeout、RTMP broken pipe 和双路配帧误差如何通过日志和测试证明？

## 14. 下一轮最小修改计划

优先级按可证明性排序：

1. **先拿到真实 BSP/板卡/模组资料**：确认 board model、kernel commit、IMX415 mode table、DTS、lane/link frequency、I2C/GPIO/regulator、IQ 和 rkAIQ headers/libs。资料缺失时不要继续扩展伪适配层。
2. **修复主链路 P1**：正确处理 RKNN NCHW、后处理索引、V4L2 plane count、sink failure propagation 和每 sink 隔离；补 mock/host tests。
3. **在目标 BSP 上确认 MPP completion/flush**：再决定 recorder queue、buffer reuse 和 DMA-BUF import；没有 completion 证据时继续保持保守 copy path。
4. **完成单路 IMX415 E0-E8 验收**：I2C probe→stream→media graph→RAW10→RKISP NV12→rkAIQ/IQ→AE/AWB→稳定性；再启用第二路。
5. **最后做双路同步和输出验收**：先软件配帧，再验证 FSIN/trigger；分别测试 RTMP、MP4、sink 断连、磁盘满、ffmpeg 不存在和慢消费者。

## 15. 审查结论

第五轮没有修改业务代码，也没有把模板、字符串检查、Python 分析器或历史文档当成板端证据。当前仓库适合作为“RK3588 双路 Camera 用户态多媒体链路 + IMX415 ISP 适配准备”的面试作品；不适合作为“已完成 IMX415 Sensor/ISP/rkAIQ/AE/AWB/同步曝光”的证明。
