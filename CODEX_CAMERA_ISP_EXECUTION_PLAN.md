# RK3588 Camera/ISP 智能相机：Codex 一次性执行计划

> **用途**：将本文件放在 `demo_multhread_decode_infer_mulmodel` 仓库根目录，交给 Codex 在该仓库中按顺序实施。
>
> **首要原则**：这是对**当前仓库的最小侵入式演进**，不是重新开发一套工程；原有 RTSP/MP4 模式必须能够回归。**完成全部可以在当前环境执行的工作才结束**。不得虚构板端、Sensor 或 ISP 的成功验证。
>
> 参考代码风格：[RK-MediaProject](https://github.com/JoeChen2me/RK-MediaProject)（`include/` + `src/`、C++17、CMake、资源生命周期显式、线程职责清晰、日志与错误处理明确）。该项目仅作风格参考，禁止直接复制不兼容的业务逻辑。

## 0. 真实约束与目标

### 0.1 业务背景

固定笼位的实验动物行为监测。**单颗 MIPI RAW Camera** 负责持续采集，RK3588 ISP 处理成像，MPP 完成 H.264 连续录像/输出，RGA + 单个 RKNN 模型负责辅助行为事件识别。录像优先，AI 允许限频和丢弃旧帧。不要为突出技术栈而硬保留四路输入、四个独立模型或四宫格。

### 0.2 目标链路

```text
                         sensor / MIPI CSI-2
                                |
                         RKISP + rkAIQ
                         (BSP / ISP userspace)
                                |
                        V4L2 NV12 capture
                                |
                    CameraBuffer (DMABUF fd)
                                |
              +-----------------+-------------------+
              |                                     |
       recorder branch                          AI branch
       MPP H.264 encode                    RGA resize/convert
              |                                     |
     existing output/sink                       RKNN inference
       (file or stream)                              |
                                               event / metadata
```

**最小可交付版**：V4L2 NV12 Camera -> MPP Encode -> 原项目现有 H.264 输出 + 可选 RGA/RKNN。录像文件输出仅在现有封装能力可复用时加入；不能把裸 H.264 当作 MP4。录像与推流以当前仓库已经具备的能力为准。

**关键边界**：ISP 是平台驱动/BSP/硬件链路，不是直接把 RTSP/MP4 URL 改为 `/dev/video0` 就算 ISP。摄像头节点、ISP 版本、rkAIQ API、IQ 文件都必须基于**实际板端**探测，不得硬编码假设。

### 0.3 不做的事情

- 不新写 Linux ISP 驱动，不自研 AE/AWB/3A 算法，不擅自修改内核、DTS 或 IQ 文件；没有板端证据时只提供探测步骤和适配点。
- 不强制迁移当前架构到 GStreamer、Qt、新线程框架或全新项目；不复制参考仓库成新工程。
- 不为了简历新增四路扩展、四宫格、强行 NPU 多核绑定；旧功能如确有用则保留在 legacy 模式，不因清理造成回归。
- 不假定 RKNN Runtime 可以直接消费任意 DMA-BUF；优先复用现有 RGA -> RKNN 输入转换路径。
- 不承诺“全链路零拷贝”：准确记录哪些段共享 fd、哪些段需要独立输出缓冲或拷贝。

### 0.4 完成定义

必须在最终报告中明确区分三类：**已在本机验证**、**有代码但缺板端验证**、**受环境阻塞**。只有满足 1～9 全部检查项且实际目标硬件验证完成才可标记 `HARDWARE_VERIFIED`。若没有 RK3588/MIPI Sensor，只能标记 `IMPLEMENTED_AWAITING_HARDWARE`，不得宣称 ISP 已跑通。

---

## 1. 【强制第一步】源码审计和基线保护

Codex 必须先读取当前仓库，不得按本文猜测的路径直接覆盖已有代码。

1. 执行 `pwd; git status --short; git branch --show-current; git log -1 --oneline`；记录修改前状态；不得丢弃用户未提交的变更。
2. 阅读 `README*`、`AGENTS.md`（若存在）、`CMakeLists.txt`、主要入口 `main`、FFmpeg demux、MPP decoder/encoder、RGA、RKNN、各队列/线程、日志和运行参数实现。用 `rg --files` 和 `rg` 定位实际文件名。
3. 形成 `docs/isp_migration/00_repository_audit.md`：列出真实模块/类/线程关系、原输入与输出格式、构建环境、RKNN 模型路径、现有测试方式、可复用模块与最小切入点。标记哪些模块来自仓库实际源码、哪些是假设。
4. 如 Git 工作区已有用户改动，单独记录并绕开它们。可新建功能分支 `feature/mipi-isp-source`（若工作区状态允许）；不做破坏性重置，不擅自 push。
5. 运行原项目已有构建/测试命令并保存结果到 `docs/isp_migration/01_baseline.md`。缺少 RK SDK 时记录具体缺失依赖，不能报告“构建成功”。

**门禁 G0**：没有 `00_repository_audit.md` 与 `01_baseline.md`，禁止开始代码重构。

## 2. 最小改造架构：先分离输入，再增加 NV12 直通

### 2.1 保留老链路

保留项目目前能够工作的 RTSP/MP4 -> FFmpeg -> MPP Decode -> RGA/RKNN -> (现有合成/编码/推流) 链路。若历史代码固定为四路/四模型，**不要第一阶段强拆**，只为新模式提供单路入口；确认回归后再局部简化。

### 2.2 新增 `mipi` 模式（配置驱动）

新增且仅新增用户可选择的输入模式：

- `legacy`（旧行为；默认值保留原运行行为）
- `mipi`（V4L2 采集 ISP 输出的 NV12，绕过 FFmpeg demux / MPP decode）
- `replay` **可选**：如果旧 MP4 输入已存在则保留，不额外开发。

新增参数尽量使用已有 CLI / 配置机制；若无机制，仅加入简洁的 `--source=legacy|mipi`、`--device=/dev/videoX`、`--width`、`--height`、`--fps`、`--ai-enable`、`--ai-fps`、`--duration-sec`、`--output` 参数（确实实现后才写文档）。不得假设 `/dev/video0` 必然是 ISP mainpath。

推荐只新增 1 个源文件和 1 个对应头文件，例如：

```text
include/v4l2_isp_camera.h   # 若仓库是 include/src 风格
src/v4l2_isp_camera.cpp
```

如果仓库已有 Camera 抽象，直接扩展它，不创建第二套平行框架。只在必要时增加 `camera_frame.h`（帧描述/所有权）。**优先修改最少文件、最少公有接口**。

### 2.3 实际帧描述/契约

以仓库真实 Frame 类型为基础扩展必要字段，示意（非强制粘贴）：

```cpp
struct VideoFrameDesc {
    int dma_fd = -1;             // fd 所有权由 BufferPool 持有
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourcc = 0;         // e.g. V4L2_PIX_FMT_NV12
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    uint32_t offset_uv = 0;      // 依据实际 plane/format 布局
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint32_t buffer_index = 0;
};
```

- 单/多平面 API 必须由设备 `VIDIOC_QUERYCAP`、格式枚举和 `G_FMT` 判断；不要假设所有 ISP 节点都使用 `VIDEO_CAPTURE_MPLANE` 或同一个 fd。
- 处理 `bytesperline`、`sizeimage`、plane offsets、buffer count，拒绝不受支持的格式，日志报告协商结果。
- `timestamp` 保留原始时间域信息；跨不同 clock domain 不可直接相减。
- fd/Buffer 所有权必须明确：`DQBUF` 后在所有异步消费者完成访问前不可 `QBUF`；关闭/停止时等待下游归还并处理超时。需要硬件完成语义，不得将 `put_frame` API 返回简单当作硬件完成。

**门禁 G1**：默认旧模式代码行为不变；新 `mipi` 入口可被选中，设备不存在时错误退出并释放资源。

## 3. V4L2 NV12 采集实现

新增适配器支持：

1. `open(O_RDWR | O_NONBLOCK)`；查询 capability；根据节点实际支持选择 single-planar 或 multi-planar capture；枚举格式，协商请求的宽高、NV12/可证明兼容的 NV12M；读回生效参数。
2. 请求可配置数量的采集 Buffer（建议初始 6，属于待调参数）；依设备支持的内存模式用 `REQBUFS` + `QUERYBUF` + `mmap`，再通过 `EXPBUF` 导出 DMA-BUF；若无法导出，明确报告 unsupported，**不冒充零拷贝**。可选有证据后加入 `V4L2_MEMORY_DMABUF` import 模式。
3. `QBUF` 预入队、`STREAMON`；使用 `poll()` 或已有 epoll 循环等待，`DQBUF` 时记录 sequence、timestamp、错误标志、间隔、队列状态。
4. 通过**既有 MPP 编码器支持的格式导入 API**交给编码分支；针对 NV12 stride/offset/alignment 验证是否兼容，必要时复用 RGA 作为兼容性/重排节点，而非重新写格式转换。
5. 停止时：先停止接收新帧 -> 通知消费者退出/排空 -> 释放引用 -> STREAMOFF -> munmap/close fd -> stop/join；避免死锁、UAF、double-QBUF、fd 泄漏。
6. `errno`/V4L2 ioctl 操作失败时打印节点、命令、错误码、格式和分辨率；具备自动退出与清理。禁止无限重试吞掉错误。

新模式只接收 **已经过 ISP 处理的 NV12/YUV 节点**；RAW Bayer 不能直接当 NV12 输入。RAW Sensor/DTS/rkAIQ 的配置属于板端先决条件，见第 8 节。

**门禁 G2**：静态检查与编译可过；有 V4L2 设备时能采集、输出逐帧序列与统计，退出后 fd/Buffer 全部回收；无设备时提供真实错误与硬件待测状态。

## 4. 双分支：录像优先，AI 可降采样

**最小改动顺序**：先只实现 `mipi -> MPP H.264 -> 已有 sink`，可以稳定工作后再复用现有 RGA/RKNN 建 AI 分支。禁止一开始重写全部线程。

1. `Capture`：V4L2 DQBUF 后分发 Frame lease，明确每个消费者完成通知。
2. `Encode`：复用现有 MPP H.264 encoder；若原输出只能接收四宫格/合成结果，增加一条直接的单路 encode 入口，而不是重写 encoder。输出保持既有推流能力；只有验证容器封装才支持 `.mp4`。
3. `AI`：复用现有 RGA resize/convert + RKNN runtime，初始只运行**一个现有可用模型**。如果当前只有四个二分类模型，不得虚构一个已训练的四分类模型；可以先配置 `model-index` 选择一个旧模型。以后更换为多类别模型独立验收。
4. AI 调度：可配置最大处理频率（如 `--ai-fps=10`，仅为默认目标，可根据设备调整）；有界队列容量 1～2，满时丢旧待处理帧，单独统计 `ai_dropped_frames`。不能让 AI 分支长期占用 Camera Buffer：RGA 预处理后尽早释放源帧；若预处理异步，等待真正完成。
5. 编码分支与 AI 分支隔离。AI 线程阻塞或模型加载失败应有明确的 fallback 策略（禁用 AI、录像继续，或者用户配置为硬失败）；不得静默忽略。
6. 事件输出最小化：JSONL/CSV（如原项目已有事件格式则复用），至少包含单调时间戳、frame sequence、model identifier、label/confidence（模型有该字段时）、推理耗时；不得输出假的行为类别。OSD 不是必须项，不为了显示检测框增加视频合成负担。

**门禁 G3**：AI 延迟可通过 `--simulate-ai-ms`（仅测试配置）注入，录制/推流链路仍继续前进；明确区分 `capture_drop`、`encode_drop`、`ai_skip`、`ai_drop_oldest`。禁用 AI 时不创建不必要的 NPU 资源。

## 5. Buffer 生命周期、背压与线程关闭

必须把跨线程 Buffer 所有权设计写入 `docs/isp_migration/02_buffer_lifecycle.md`，并严格照此实现：

- 每个 V4L2 buffer 保持唯一 owner/slot 状态（Queued -> Dequeued -> InUse -> Queued）；内存与 fd 由采集池拥有，消费者只借用 lease，不自行 `close` fd。
- 如果多个消费端共享同一个源 Buffer，通过受保护引用计数或已存在的同步机制等待所有 consumer 完成；不能单靠 C++ 对象析构认定硬件读取结束。
- 优先采用 **Camera -> encoder + RGA** 双消费者；RGA 输出属于独立小型 AI Buffer 池。相机输入 frame 在 encoder 与 RGA 都完成后及时归还，不等待 RKNN 推理。
- 对 MPP `put_frame/get_packet` 和输入 Buffer 复用时机调查实际 SDK 语义；必要时独立持有编码输入缓冲或加入可验证的完成机制。禁止假设 `encode_put_frame` 返回就能 `QBUF`。
- 任何队列必须有容量上限、溢出策略、退出通知；录像支路拥塞要单独计数与报错，不能偷偷 drop oldest 却声称无丢帧。
- 容错：设备断开、`poll` 超时、`DQBUF` 出错、输出 sink 关闭、Ctrl+C、初始化半途失败均能有界退出。

**门禁 G4**：用模拟或 mock 测试证明 Buffer 不会重复归还、不会在消费者持有时归还、关闭时不死锁；计数统计与实际行为一致。

## 6. ISP/rkAIQ 接口：严格区分系统调试与工程代码

优先使用已安装的厂商 BSP、rkAIQ 服务、配置工具，尽量**不把特定版本 rkAIQ SDK 硬绑定到原项目**。

1. 实际板端检查：RK3588/3588S 型号、内核/BSP 版本、Camera Sensor 型号、I2C 探测、DTS endpoint、`media-ctl -p`、`v4l2-ctl --list-devices`、各 `/dev/video*` 的格式、`dmesg` Camera/ISP 日志、rkAIQ 服务与 IQ 路径。保存输出（脱敏）到 `docs/isp_migration/hardware/`。
2. 明确采集节点是否为 ISP 输出 NV12 mainpath/selfpath，不得只凭 `/dev/video0` 命名判断。
3. 提供 `scripts/isp_probe.sh`：只做安全的只读探测（可选工具缺失时友好提示），**禁止脚本自动写 DTS、修改 IQ 或覆盖相机配置**。
4. 提供 `docs/isp_migration/03_isp_bringup.md`：从 Sensor -> CSI -> ISP -> V4L2 NV12 的调试清单，区分 RK3588 具体 BSP 差异；列举 AE/AWB、曝光/gain 操作和回读方法。命令必须与当前实测 SDK 对应；无法确认的 API 写 `TODO(board-specific)`，不要编造函数或路径。
5. 如果当前 BSP 可用且已验证 rkAIQ 控制 API，再选择性新增最小 `IspControl` 包装实现 `auto exposure / manual exposure / gain`，由可选 CMake feature flag 控制；否则把该功能标记为 `BOARD_REQUIRED`，**不要用空实现谎称 ISP 控制成功**。
6. 对弱光/曝光实验，记录镜头/灯光/距离、shutter/gain、帧率、平均亮度、识别表现；没有测试图像和标签，不报告准确率提升。

**门禁 G5**：板端有真实探测结果时才能标记 Sensor/ISP/rkAIQ 已联调；无硬件只能完成脚本和文档，不能用 mock 代替硬件验收。

## 7. 构建、测试、性能与回归

建立最小测试矩阵（优先沿用仓库测试框架；没有则写轻量单元测试/可独立执行的测试工具）：

| 测试 | 执行环境 | 通过条件 |
|---|---|---|
| 旧 RTSP/MP4 输入回归 | 原项目可用环境 | 原输入、旧模型、输出行为不回退 |
| `--help` / 非法参数 | 普通开发机 | 选项明晰、错误码非零 |
| mipi 不存在设备 | 普通开发机 | 说明具体错误，进程正常释放资源 |
| V4L2 单/多平面格式处理 | mock/真实设备 | 类型正确、不越界、拒绝错误布局 |
| Buffer 双消费者竞争与回收 | mock | 无重复归还、无提前归还 |
| AI 故意卡顿 | 模拟/板端 | AI 可丢旧帧，录像支路不因 AI 同步等待 |
| Ctrl+C / 半初始化异常退出 | 可运行环境 | 线程结束、资源释放、无死锁 |
| Camera -> MPP -> sink | RK3588 + MIPI Sensor | 实际正常编码输出，统计采集/编码 FPS |
| ISP / AE 控制 | RK3588 + 对应 BSP | 有真实调试记录/参数回读 |
| 30min/1h 稳定性 | RK3588 | 实测记录帧率、CPU、RSS、丢帧；不预设零丢帧 |

建议最少埋点：`capture_fps`, `encode_fps`, `ai_fps`, `capture_dqbuf_ms`, `encode_latency_ms`, `ai_infer_ms`, `v4l2_inflight`, `encode_queue_depth`, `ai_queue_depth`, `ai_skip`, `ai_drop_oldest`, `capture_error`, `encode_error`, `rss_kb`。对无法获得的指标不输出虚假数值。

**门禁 G6**：执行 `cmake -S . -B build` / `cmake --build build -j` 或仓库对应命令，现有测试和新增测试全部跑；缺依赖时说明依赖与不能执行的具体测试，不能把 mock 编译成功写成板端通过。把日志/结果保存到 `docs/isp_migration/04_test_report.md`。

## 8. 板端实际执行清单（有真实 RK3588 + MIPI RAW Sensor 时必须完成）

先只读探测（根据发行版安装工具）：

```bash
uname -a
cat /proc/device-tree/model 2>/dev/null || true
ls -l /dev/media* /dev/video* 2>/dev/null || true
v4l2-ctl --list-devices
media-ctl -p -d /dev/media0   # 需要枚举所有 media 节点；这里仅示例
v4l2-ctl -d /dev/videoX --all  # X 由探测结果决定
v4l2-ctl -d /dev/videoX --list-formats-ext
```

1. 识别实际的 MIPI Sensor 与 ISP 节点，确认 Sensor 对应的 driver、当前 mode、分辨率、RAW pattern、CSI lane 和 link status。
2. 按 BSP 文档验证 Camera ISP 采集 NV12；**不要改用 `/dev/videoX` 后直接宣布 ISP 已启用**。
3. 验证实际采集格式的 `bytesperline`、buffer type、buffer count、multi-plane layout、DMA-BUF export 与 downstream import；不支持的分支应正确报错或有真实已测试 fallback。
4. 先执行 `mipi` + `--ai-enable=false` 编码输出，再执行启用单模型及 AI 过载测试。
5. 根据本机实际 rkAIQ 版本设置/回读曝光，保留对比图像与真实日志。随后跑稳定性实验并保存证据。
6. 缺硬件、缺 IQ 或驱动不通时：在 `04_test_report.md` 写 blocker、已经完成的软件检查、下一条需在板端运行的准确命令；不要捏造验收数字。

## 9. 文档交付清单

Codex 应在仓库内完成以下文件（已有相近文件则更新，避免重复）：

```text
README.md                                  # 新/旧两条链路 + 实际可运行命令
CODEX_CAMERA_ISP_EXECUTION_PLAN.md        # 本计划，维护完成状态
docs/isp_migration/00_repository_audit.md # 真实源码结构与设计切入点
docs/isp_migration/01_baseline.md         # 修改前测试记录
docs/isp_migration/02_buffer_lifecycle.md # Buffer 与线程生命周期图
docs/isp_migration/03_isp_bringup.md      # Camera/ISP 板端调试手册
docs/isp_migration/04_test_report.md      # 构建、测试、性能、阻塞项
scripts/isp_probe.sh                       # 只读探测脚本
```

真实代码文件由第 1 步审计决定，不按预期随意新增层次。代码风格参照 RK-MediaProject：C++17、局部 RAII、清晰的 init/start/stop/deinit 对称关系、`include/` / `src/` 分离（如适配现有目录）、易追踪线程名称、显式错误码、简洁日志、CMake feature flag 控制可选 RK SDK/rkAIQ 依赖。**不是让你把 RK-MediaProject 复制到本仓库**。

## 10. Codex 执行协议（必须遵守）

- 连续推进阶段 1～9：**完成一个阶段 -> 运行可执行验收 -> 修复失败 -> 更新文档 -> 进入下个阶段**。不要每一步都询问用户，不要做无意义的阶段性停止。
- 遇到实际缺少硬件、RKNN 模型、厂商闭源库或 root 权限时，继续完成所有**不依赖该阻塞项**的工作，包括 mock、编译、测试、文档；只把真实阻塞项放入最终报告。
- 尽量小的补丁：先复用原来的编解码、RGA、RKNN、队列和日志；避免改变旧构建接口与默认行为，避免不必要的格式化全仓库。
- 禁止写假的 `rkAIQ` 接口、假的性能指标、假的 Sensor 驱动或永远返回成功的 stub；如果本机无法做板端验收，要公开标记未验证。
- 修复每个编译错误、clang-format 相关文件、运行可用测试；保留所有测试命令与退出码。
- 当阶段全结束后输出：`git diff --stat`、关键改动文件、旧模式回归情况、新模式可执行命令、各门禁 G0～G6 结果、实际板端验证状态、剩余阻塞项和复现步骤。
- **停止条件**：仅当能在当前环境做的实施/构建/测试/文档均完成，且所有不可执行项被明确证明为外部阻塞时停止。若板端可用，必须完成全部板端验证，不允许提前声称完成。

## 11. 完成状态（由 Codex 执行后填写，初始都未完成）

- [x] G0 源码审计与基线
- [x] G1 旧模式保留、新输入切换
- [x] G2 V4L2 ISP NV12 采集（代码完成，待板端设备）
- [x] G3 录像优先 + 可选单模型 AI（代码完成，待板端设备/模型）
- [x] G4 Buffer 生命周期与压力测试（mock 源码完成，待 Linux 编译运行）
- [x] G5 rkAIQ/ISP 板端联调或有证据的外部阻塞（当前环境缺硬件，已记录）
- [x] G6 构建测试、回归、性能与报告（构建命令/阻塞已记录）
- [x] 文档、配置、运行示例、代码格式完成（板端格式化/构建待执行）

**状态取值（最终必须二选一，禁止口头“全部已完成”）**：`HARDWARE_VERIFIED` / `IMPLEMENTED_AWAITING_HARDWARE`

当前状态：`IMPLEMENTED_AWAITING_HARDWARE`。软件适配和文档已完成，真实 ISP/Sensor/rkAIQ 验证需在 RK3588 板端执行。


