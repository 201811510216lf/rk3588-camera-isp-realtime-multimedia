# Codex 专项实施任务书：RK3588 RKISP30 / RkAiq 软件接入与双路 Camera Pipeline 集成

> **使用方法**：将本文件放到仓库根目录，命名为
> `CODEX_RKISP_IMPLEMENTATION.md`，然后让 Codex 完整阅读并执行。\
> **执行模式**：按阶段连续自动执行，不等待用户确认。每阶段读取前序报告、修改代码、构建/测试、生成报告后继续。没有真实
> BSP/SDK/开发板时，完成所有可独立完成的软件实现；硬件运行项标记
> `BLOCKED_HARDWARE`，不得伪造成功。\
> **核心目标**：把现有 `AiqController` 从"服务/运行库/IQ
> 目录发现工具"演进为可插拔、版本适配、可验证的 RK3588 ISP
> 控制层，并与现有双路 `V4L2IspCamera` / `DualCameraPipeline`
> 集成。不得重写现有双路 Camera Pipeline。

------------------------------------------------------------------------

# 0. 项目背景与当前仓库事实

本项目：
<https://github.com/201811510216lf/rk3588-camera-isp-realtime-multimedia>

当前 README 的主程序是
`main.cpp -> DualCameraPipeline`，目标数据路径为双路 V4L2 NV12/NV12M →
软件时间戳配帧 → 每路可选 RKNN → OSD/左右拼接 → MPP H.264 →
H.264/RTMP/分段 MP4。README 明确说明 Sensor/CSI/RKISP/rkAIQ 是 V4L2
图像到达前的外部板级前置条件，当前程序本身不负责初始化 rkAIQ。

当前 `docs/BSP_CAMERA_ISP.md` 记录的已有实现和边界： - `V4L2IspCamera`
已负责 NV12/NV12M capability/format/plane/stride/bytesused/timestamp
校验、MMAP lease 和安全停止。 - `media_graph_validator`
是只读工具，使用目标板已有
`media-ctl -p`、`v4l2-ctl --all --list-formats-ext`
或离线保存的原始命令输出。 - `isp_control_cli` / `AiqController`
当前只做服务/运行库/IQ 根目录发现和状态转储。 - 没有真实 rkAIQ backend
时，AE、曝光、增益、AWB 写入返回 `unsupported` 并 fail-closed。 -
`scripts/isp_probe.sh`、`scripts/isp_capture_experiment.sh`、`scripts/analyze_isp_samples.py`、`scripts/aiq_sample_capture.sh`
已存在。 - 当前没有可用 BSP checkout、Kernel tree、Sensor
模组资料、`/dev/media*`、`/dev/video*`、rkAIQ headers/libraries 或 IQ
文件；Sensor 型号未知。 - 不得猜测 Sensor 型号、I2C 地址、GPIO、MIPI
lane、Media Entity 名称或不存在的 rkAIQ 函数。 - 当前 `myDemo`
依赖外部板级 ISP/RkAiq 环境；本任务重点是完善软件 ISP
HAL/控制适配与接入，不是编写 RKISP 内核驱动。

以上为审查起点。执行时必须重新检查 main
分支工作树实际内容，若当前源码与上述描述不同，以实际源码为准，并在报告中指出差异。

------------------------------------------------------------------------

# 1. 目标架构与职责边界

## 1.1 图像数据路径

``` text
Sensor 0 / Sensor 1
        |
     MIPI CSI
        |
    RKCIF / CIF
        |
   RK3588 RKISP30
   (hardware ISP)
        |
 V4L2 ISP output nodes
   NV12 / NV12M
        |
 V4L2IspCamera x 2
        |
 DualCameraPipeline
        |
 FramePairer / dispatch
        |
 +------+-------------------+
 |                          |
 RKNN AI path            Video path
 |                          |
 rat detection          RGA / OSD / compose
 behavior analysis           |
 |                       MPP H.264
 +------------+-------------+
              |
        event/video index
              |
      MP4 / RTMP / 24h manager
```

## 1.2 RkAiq 控制路径（不是图像串行节点）

``` text
Media Controller topology + Sensor entity
                 |
          Sensor identity
                 |
          IQ file selection
                 |
           RkAiq backend
                 |
       AE / AWB / IQ parameters
                 |
       RKISP driver + Sensor controls
```

RkAiq 是 ISP 3A/IQ 控制软件，处理统计信息并控制 ISP/Sensor 参数。不要把
RkAiq 画成 RAW 数据经过的串行图像处理节点。

## 1.3 本任务的范围

重点实现： 1. ISP 设备/Media Graph 发现和诊断； 2. Sensor entity 与 V4L2
ISP 输出节点关联； 3. RKISP30 平台能力和输出节点验证； 4. RkAiq
服务发现、所有权检查、SDK 版本检测； 5. 可插拔 RkAiq API backend； 6. 双
Camera 的 AIQ context 生命周期管理； 7.
AE/AWB/曝光/增益等能力查询、设置、回读（仅 SDK 实际支持时）； 8. ISP
状态快照、配置/IQ 文件检查、运行日志； 9. 将 ISP readiness/health
与现有双路 V4L2 Camera Pipeline 集成； 10. 完整的主机测试、SDK
可选构建和板端验收文档。

不在本任务实现： - RKISP 内核驱动； - RKCIF/MIPI 内核驱动； - Sensor
内核驱动； - Device Tree 板级适配； - 未知 Sensor 的寄存器表； -
自行实现 ISP 算法替代 RKISP； - 未经证实的硬件同步； - 未经证实的
DMA-BUF 零拷贝。

------------------------------------------------------------------------

# 2. 必须遵循的 RK3588 / RkAiq API 规则

## 2.1 先检查 SDK，再写 backend

RKAIQ API 在不同 Rockchip BSP / rkaiq release / ISP generation
之间可能存在差异。必须使用当前目标 SDK
的真实头文件和库，不能仅凭网络示例或其他版本代码复制函数。

常见 RKAIQ UAPI2 系统控制 API 名称包括（实际声明以 SDK 头文件为准）： -
`rk_aiq_uapi2_sysctl_enumStaticMetas` -
`rk_aiq_uapi2_sysctl_getBindedSnsEntNmByVd` -
`rk_aiq_uapi2_sysctl_preInit` - `rk_aiq_uapi2_sysctl_init` -
`rk_aiq_uapi2_sysctl_prepare` - `rk_aiq_uapi2_sysctl_start` -
`rk_aiq_uapi2_sysctl_stop` - `rk_aiq_uapi2_sysctl_deinit`

只有当目标 SDK 中存在对应声明、链接库和兼容的 ISP
平台支持时，才能调用。函数名、参数和调用顺序以实际 SDK headers/examples
为唯一依据。不得假设上述所有 API 在目标版本中都具有完全相同签名。

## 2.2 RkAiq 所有权：绝不双重初始化

先检查厂商 BSP 是否已经通过 systemd、IspServer、camera daemon
或其他进程拥有 ISP/AIQ context。

必须实现明确的运行模式： - `EXTERNAL_SERVICE`: ISP/AIQ
由外部服务管理，本应用只发现、查询和诊断；不得再 init/start 同一 camera
的 AIQ context。 - `IN_PROCESS`: 本应用通过目标 SDK 创建和管理 AIQ
context；必须确认没有外部进程同时管理同一个 ISP/camera。 -
`UNAVAILABLE`: SDK/服务均不可用，状态明确失败。

默认策略必须安全：如果检测到可能已有外部服务拥有 ISP，默认选择
`EXTERNAL_SERVICE` 或 `OWNERSHIP_CONFLICT`，不得擅自启动第二个 context。

## 2.3 IQ 文件必须严格匹配

不得只检查目录存在。需要验证： - 选定 Sensor entity / sensor name； - IQ
文件路径； - 文件是否存在、可读； - 文件名/元数据是否与 Sensor 匹配（以
SDK 提供的机制为准）； - IQ 版本/SDK 兼容性（如果可查询）； -
当前运行模式（Normal/HDR）与配置匹配； - 实际加载状态必须来自 SDK
返回值或服务状态。

没有真实加载证据时报告 `NOT_LOADED` 或 `NOT_MEASURED`，不能报告 `OK`。

------------------------------------------------------------------------

# 阶段 1：重新审查现有 ISP 代码，建立改造基线

## 工作

1.  阅读并核对：
    -   `docs/BSP_CAMERA_ISP.md`
    -   `include/` 中 ISP/V4L2/Aiq 相关头文件
    -   `lib/` 中 `AiqController`、Media Graph、ISP 工具实现
    -   `tools/isp_control_cli.cpp`
    -   `tools/media_graph_validator.cpp`
    -   `scripts/isp_probe.sh`
    -   `scripts/isp_capture_experiment.sh`
    -   `scripts/aiq_sample_capture.sh`
    -   `main.cpp` / `DualCameraPipeline` / `V4L2IspCamera`
    -   CMakeLists 和测试
2.  对每个 ISP 功能标记：
    -   `IMPLEMENTED`
    -   `PARTIAL`
    -   `DISCOVERY_ONLY`
    -   `SDK_BACKEND_MISSING`
    -   `MOCK_OR_TEST`
    -   `BLOCKED_HARDWARE`
3.  查找重复的 ISP
    状态结构、配置字段和命令行参数，统一已有实现，避免重写。
4.  识别 `AiqController` 当前 unsupported 的操作及其调用链。
5.  检查 media graph validator 是否仅做只读检查，确认不会修改 media
    link/format/control。
6.  检查双 Camera 与 AIQ 的 camera id、sensor entity name、video node
    之间是否有可靠映射；没有则列为缺失，不要按数组序号猜。

## 产物

-   `Plan/RKISP/01_CURRENT_ISP_AUDIT.md`
-   `Plan/RKISP/01_API_GAP_MATRIX.md`
-   `Plan/RKISP/01_CAMERA_ID_MAPPING.md`

完成后自动进入阶段 2。

------------------------------------------------------------------------

# 阶段 2：定义 ISP HAL / 配置 / 状态模型

## 2.1 目标目录

结合当前仓库结构最小改动，建议将 ISP 子系统整理为：

``` text
include/isp/
  isp_types.h
  isp_config.h
  isp_status.h
  media_graph_inspector.h
  sensor_registry.h
  rkisp_capability.h
  rkaiq_backend.h
  rkaiq_controller.h
  isp_health_monitor.h

src/isp/
  isp_config.cpp
  media_graph_inspector.cpp
  sensor_registry.cpp
  rkisp_capability.cpp
  rkaiq_controller.cpp
  isp_health_monitor.cpp
  backends/
    external_rkaiq_service_backend.cpp
    rockchip_rkaiq_backend.cpp

tools/
  media_graph_validator.cpp
  isp_control_cli.cpp

configs/isp/
  isp_config.example.yaml
  sensors.example.yaml

Plan/RKISP/
```

若现有工程尚未采用
`src/`，可以按当前增量重构约定整理；不要制造重复的同名类。

## 2.2 统一数据模型

定义： - `IspCameraId` - `SensorIdentity` - `SensorEntity` -
`IspDeviceInfo` - `MediaEntityInfo` - `MediaLinkInfo` -
`IspOutputNode` - `IspWorkingMode`（Normal/HDR 等仅按 SDK 能力） -
`IspAiqOwnershipMode` - `IspRuntimeState` - `IspHealth` -
`IspControlCapabilities` - `IspControlResult`

状态必须区分： - `UNKNOWN` - `DISCOVERED` - `READY` - `RUNNING` -
`DEGRADED` - `UNAVAILABLE` - `OWNERSHIP_CONFLICT` - `CONFIG_MISMATCH` -
`ERROR`

所有控制操作必须返回结构化结果，至少包含： - status； - operation； -
camera id； - error code； - human-readable message； - backend
name/version； - whether hardware result was confirmed。

禁止把"命令已发送"直接等同于"硬件参数已生效"。

## 2.3 接口设计

优先复用 `AiqController`，但内部通过 backend 接口隔离版本差异：

``` cpp
class IRkAiqBackend {
public:
    virtual ~IRkAiqBackend() = default;

    virtual BackendInfo getBackendInfo() const = 0;
    virtual Result initialize(const AiqCameraConfig&) = 0;
    virtual Result prepare() = 0;
    virtual Result start() = 0;
    virtual Result stop() = 0;
    virtual Result deinitialize() = 0;

    virtual Result queryCapabilities(IspControlCapabilities&) = 0;
    virtual Result getStatus(IspRuntimeState&) = 0;

    virtual Result setAeMode(AeMode) = 0;
    virtual Result setExposureTimeUs(double) = 0;
    virtual Result setAnalogGain(double) = 0;
    virtual Result setAwbMode(AwbMode) = 0;
};
```

这是接口意图，需结合现有 API 和 SDK 能力调整。对 SDK
不支持的功能返回明确 `UNSUPPORTED`，不得伪造。

还需要： - `IMediaGraphInspector` - `ISensorRegistry` -
`IIspHealthProvider`

生成 `Plan/RKISP/02_ISP_HAL_DESIGN.md`，自动进入阶段 3。

------------------------------------------------------------------------

# 阶段 3：Media Controller / Sensor / RKISP 发现与能力验证

## 目标

将现有只读 `media_graph_validator` 和 probe 脚本升级为可靠、结构化的 ISP
发现模块。

## 3.1 Media Graph

支持通过配置指定： - media device path； - expected sensor entity； -
expected ISP entity； - expected capture node； - camera logical id。

发现信息至少包括： - media device； - entity name/function； - pad
index/direction； - link source/sink； - link enabled state； -
subdev； - video node； - media bus format（能读取时）； - capture node
format/capabilities。

不要把 entity 名称、video node 编号写死为某块板卡值。

## 3.2 只读原则

本阶段只发现和验证： - 不调用 `MEDIA_IOC_SETUP_LINK`； - 不写 subdev
format； - 不设置 V4L2 control； - 不启停 stream； - 不写 Sensor I2C； -
不改 Device Tree。

如确实需要配置 link/format，另行生成显式的人工执行步骤，不在自动 probe
中执行。

## 3.3 Sensor 与 ISP 输出节点映射

建立配置驱动映射：
`logical camera id -> sensor entity -> media device -> ISP output node -> expected pixel format`

必须有证据才能建立映射。不能简单假设 Camera 0 就是 `/dev/video0`。

## 3.4 输出格式验证

确认 ISP 输出节点支持： - NV12； - NV12M； - 或配置声明的其他格式。

核验 width/height/fps 和多平面布局。对 RAW capture node 和 ISP processed
output node 必须区分。

## 3.5 产物

-   `Plan/RKISP/03_MEDIA_GRAPH_DISCOVERY.md`
-   `Plan/RKISP/03_SENSOR_ISP_MAPPING.md`
-   `Plan/RKISP/03_OUTPUT_FORMAT_VALIDATION.md`

无设备节点时，离线 parser 单元测试必须完成；板端发现标记
`BLOCKED_HARDWARE`。自动进入阶段 4。

------------------------------------------------------------------------

# 阶段 4：实现 RkAiq 后端与生命周期管理

## 目标

这是本任务重点。把当前仅做服务/运行库/IQ 发现的 `AiqController`
演进为可插拔的 RkAiq 控制模块。

## 4.1 SDK 检测和 CMake

增加独立构建选项： - `ENABLE_RKAIQ=OFF` 默认； - `RKAIQ_ROOT`； -
`RKAIQ_INCLUDE_DIR`； - `RKAIQ_LIBRARY_DIR`； - 必要的 library list 由
SDK 实际构建产物确定。

要求： - SDK OFF 时核心软件和测试可编译； - SDK ON 但 headers/libs
缺失时 CMake 明确失败并提示缺项； - 不使用固定的 `../rknn`
或其他相对目录猜测 SDK； - 不复制网上其他版本的头文件到仓库伪装 SDK； -
不把未链接的代码标记为已实现。

## 4.2 版本与能力检测

实现启动时 backend info： - backend 类型； - RKAIQ SDK 版本（若 SDK 有
API 可查询）； - 头文件/库检测信息； - ISP generation/platform
capability（仅有证据时）； - 支持的 control capability； - service
ownership mode。

若无法读取 SDK 版本，报告 `UNKNOWN`，不得猜版本。

## 4.3 两种所有权模式

### EXTERNAL_SERVICE

-   检查 systemd/service/process；
-   检查已运行 AIQ 服务；
-   不调用 init/start/stop/deinit；
-   只调用厂商允许的客户端接口（若有）或只做状态发现；
-   如果没有合法 IPC/API，控制操作返回 `UNSUPPORTED`。

### IN_PROCESS

-   仅当 SDK 支持、无外部 AIQ 所有者、camera sensor entity 和 IQ
    文件已确认时启用；
-   按目标 SDK 示例和头文件规定的顺序执行 preInit/init/prepare/start；
-   stop 后 deinit；
-   任一步失败必须回滚已完成的步骤；
-   析构不得在 running state 直接 deinit；
-   禁止同一 ISP/camera 被两个进程重复初始化。

不得自行假定 IspServer 和进程内 context 可以同时操作同一个 ISP。

## 4.4 两路 Camera Context 管理

实现 `RkAiqManager`： - 每个 camera logical id 对应明确 sensor entity
name； - 根据 BSP topology 决定独立 context 或 camera group； -
不因双路就盲目创建两个 context； - 不把 `camera_id` 直接当作 RkAiq 的
physical camera id； - 对 camera group API 仅在 SDK 和 BSP
明确支持时启用； - 两路初始化/停止部分失败时有可预测回滚和状态报告。

## 4.5 真实 API 接入

在目标 SDK headers/examples 中查找系统控制 API。常见 UAPI2
名称可能包括： - `rk_aiq_uapi2_sysctl_enumStaticMetas` -
`rk_aiq_uapi2_sysctl_getBindedSnsEntNmByVd` -
`rk_aiq_uapi2_sysctl_preInit` - `rk_aiq_uapi2_sysctl_init` -
`rk_aiq_uapi2_sysctl_prepare` - `rk_aiq_uapi2_sysctl_start` -
`rk_aiq_uapi2_sysctl_stop` - `rk_aiq_uapi2_sysctl_deinit`

**以上仅为 API 名称线索，必须以当前 SDK
的实际声明、示例和库为准，逐项核对函数签名、调用顺序、返回码、线程约束和版本差异。**

不要根据网上另一版本的函数签名硬写。若 SDK 不在环境中： - 完成
interface、CMake 检测、状态机、fake backend 单元测试； - 保留
`RockchipRkAiqBackend` 的独立适配目录； - 将真实 API 实现标为
`BLOCKED_HARDWARE`； - 不要创建一个空类并返回 success。

## 4.6 IQ 文件验证

配置项： - `iq_root`; - `iq_file`; - `sensor_entity`; -
`working_mode`; - `expected_sensor_name`（若已知）。

启动前检查文件存在、可读、配置与 sensor identity 匹配。SDK
支持时查询实际加载状态。无法确认加载状态时 `NOT_MEASURED`。

不允许只因目录存在就报告 IQ loaded。

## 4.7 控制 API

在 SDK 实际支持时实现： - 查询 AE mode； - 设置 AE auto/manual； -
查询曝光时间； - 设置曝光时间； - 查询 gain； - 设置 gain； - 查询 AWB
mode； - 设置 AWB auto/manual； - 获取 3A 状态/统计（若 API 支持）； -
dump 当前 ISP/AIQ state。

所有 set 操作必须： 1. 检查 capability； 2. 校验参数范围； 3. 调用
backend； 4. 检查返回码； 5. 尽可能 read-back； 6.
返回"请求成功"与"硬件生效已确认"的区别。

不要默认手动曝光设置一定能成功；HDR/Normal mode 的参数结构可能不同，需按
SDK 处理。

## 4.8 CLI

扩展 `isp_control_cli`： - `--isp-status` - `--camera-id` -
`--list-cameras` - `--list-sensors` - `--list-controls` - `--iq-root` -
`--dump-isp-state` - `--ae-auto` - `--ae-manual` - `--exposure-us` -
`--gain` - `--awb-auto` - `--awb-manual`

无真实 backend 时控制命令必须非零退出并打印 `UNSUPPORTED` /
`BACKEND_UNAVAILABLE`，不能打印成功。

## 4.9 测试

Fake RkAiq Backend 测试： - 正常 init/prepare/start/stop/deinit； - init
失败回滚； - prepare 失败回滚； - start 失败回滚； - 重复 init； -
running 时禁止 deinit； - external service ownership conflict； - IQ
missing/mismatch； - unsupported control； - set 后 readback
mismatch； - 双 Camera 部分失败。

产物： - `Plan/RKISP/04_RKAIQ_BACKEND.md` -
`Plan/RKISP/04_RKAIQ_LIFECYCLE.md` -
`Plan/RKISP/04_RKAIQ_API_COMPATIBILITY.md`

自动进入阶段 5。

------------------------------------------------------------------------

# 阶段 5：RkAiq / ISP 状态与双路 V4L2 Pipeline 集成

## 目标

使现有 `DualCameraPipeline` 在启动采集前检查 ISP readiness，但不把 ISP
控制逻辑塞入 Capture 类。

## 启动顺序

``` text
Application Config
       |
       v
Media Graph Discovery
       |
       v
Camera/Sensor Mapping
       |
       v
RkAiq Ownership Check
       |
       v
IQ Configuration Validation
       |
       v
ISP Readiness Check
       |
       v
V4L2 Camera Open/Format/Buffer Setup
       |
       v
Camera STREAMON
       |
       v
DualCameraPipeline
```

注意： - 若厂商系统要求 RkAiq 在 Camera stream 前/后启动，必须按目标 BSP
文档执行，不得强制使用通用顺序。 - 若外部 service 已管理
AIQ，应用只检查其状态，不重复 init。 - 如果 AIQ
状态不可确认，可根据配置选择 `strict_isp_ready=true` 时
fail-fast，或只允许显式 `allow_unverified_isp=true`
的诊断模式；默认不能静默当作 ready。

## Camera 配置

每路 Camera 配置： - logical camera id； - media device； - sensor
entity； - video node； - width/height/fps； - pixel format； - expected
ISP output； - AIQ ownership mode； - IQ root/file； - working mode。

## Pipeline 集成

-   保留现有 `V4L2IspCamera`；
-   保留现有 `DualCameraPipeline`；
-   增加 `IspReadinessProvider` / `IspCameraRegistry` 依赖；
-   不要在 V4L2 Capture Thread 中调用耗时 AIQ 初始化；
-   启动和停止顺序由 `PipelineManager` 协调；
-   ISP 未 ready 时输出明确错误码和诊断；
-   Camera 0/1 映射必须基于真实 sensor entity 和 media graph
    配置，不按节点序号推断。

产物： - `Plan/RKISP/05_DUAL_CAMERA_ISP_INTEGRATION.md` - 更新
`docs/BSP_CAMERA_ISP.md` - 更新 `CURRENT_ARCHITECTURE.md`

自动进入阶段 6。

------------------------------------------------------------------------

# 阶段 6：ISP 质量诊断和 24 小时运行观测

## 目标

让 ISP 调试不只是"能出图"，还能够记录真实状态和质量变化。

复用现有： - `isp_probe.sh` - `isp_capture_experiment.sh` -
`analyze_isp_samples.py` - `aiq_sample_capture.sh` -
`media_graph_validator` - `isp_control_cli`

增强： - Camera/ISP readiness； - AE/AWB mode 和状态（SDK 能提供时）； -
exposure/gain readback（SDK 能提供时）； - IQ 文件身份/版本/加载状态； -
ISP output FPS； - frame interval； - frame sequence gap； - NV12 luma
histogram； - luma mean/stddev； - clipping ratio； - 简单噪声估计； -
frame-to-frame brightness variation； - 统计指标的采集条件和来源。

必须区分： - 由真实设备采集； - 由离线文件分析； - 由 fake backend
测试； - 未测量。

不要将简单亮度/噪声指标称为专业 IQ 标定或完整画质评价。

对 24 小时监控场景，增加： - ISP service heartbeat； - Camera capture
heartbeat； - stream stall detection； - AE/AWB state snapshot； - ISP
error counter； - camera reconnect status。

产物： - `Plan/RKISP/06_ISP_DIAGNOSTICS.md` - 更新
`scripts/aiq_sample_capture.sh` - 更新
`scripts/analyze_isp_samples.py` - 更新 `docs/BSP_CAMERA_ISP.md`

自动进入阶段 7。

------------------------------------------------------------------------

# 阶段 7：构建、测试和静态审查

## 主机默认构建

确保不安装 RKAIQ SDK 时，以下内容仍可构建： - ISP interface； - Media
graph parser； - ISP configuration； - Fake backend； - ISP readiness
state machine； - CLI 的 discovery-only 模式； - 现有主机测试。

## SDK 构建

提供 `ENABLE_RKAIQ=ON` 的可选构建路径。SDK 缺失时明确报错；SDK
存在时检查 include/library/ABI 和目标架构，不得错误链接 x86 库到 aarch64
或反向。

## 测试

至少测试： - Media graph parser； - sensor/entity mapping； - ISP config
validation； - IQ path validation； - AIQ ownership state； - AIQ
lifecycle rollback； - dual camera partial failure； - unsupported
controls； - CLI exit codes； - readiness gating； - ISP health state
transitions。

执行 CMake build、CTest、Python unit tests、clang-format 和可用
sanitizer。

产物： - `Plan/RKISP/07_TEST_REPORT.md` -
`Plan/RKISP/07_BUILD_MATRIX.md`

自动进入阶段 8。

------------------------------------------------------------------------

# 阶段 8：板端验收手册和最终交付

生成 `docs/RK3588_RKISP_BRINGUP.md`，明确未来使用瑞芯微官方 RK3588 BSP
时的验收步骤：

1.  确认 Kernel/BSP commit 和 RKISP driver。
2.  确认 Sensor driver 与 Device Tree。
3.  `media-ctl -p` 保存真实拓扑。
4.  `v4l2-ctl --list-devices`。
5.  确认 sensor entity 与 ISP output node。
6.  确认 RkAiq service/ownership。
7.  确认 Sensor 对应 IQ 文件。
8.  检查 AE/AWB 状态。
9.  采集 ISP 输出 NV12/NV12M。
10. 检查 width/height/stride/plane/bytesused/timestamp/sequence。
11. 单路持续采集。
12. 双路同时采集。
13. 接入现有 AI/MPP Pipeline。
14. 记录 FPS、drop、latency、CPU/RSS。
15. 运行 24h/72h 长稳测试。

命令必须按实际发现的设备节点填写占位符，不能写死 `/dev/video0`。

最终更新： - `docs/BSP_CAMERA_ISP.md` - `CURRENT_ARCHITECTURE.md` -
`PROJECT_STATUS.md` - `README.md` - `INTERVIEW_GUIDE.md`

生成： - `Plan/RKISP/FINAL_RKISP_DELIVERY_REPORT.md` -
`Plan/RKISP/FINAL_HARDWARE_BLOCKERS.md`

最终报告必须清楚区分： - 主机代码已实现； - fake backend 测试通过； -
RKAIQ SDK 编译通过； - RK3588 板端已验证； - 尚待 Sensor/BSP/硬件验证。

------------------------------------------------------------------------

# 全局禁止事项

1.  不得编写 RKISP 内核驱动替代厂商驱动。
2.  不得虚构 Sensor 型号、I2C 地址、GPIO、MIPI lane 或 media entity。
3.  不得凭网络示例猜测 RKAIQ API 函数签名。
4.  不得在已有 IspServer/AIQ service 管理同一 ISP 时重复 init。
5.  不得把 RkAiq 画成串行图像处理节点。
6.  不得将 RAW capture node 误认为 ISP NV12 output node。
7.  不得将 V4L2 NV12 重新送入 Software ISP RAW 算法。
8.  不得把 Fake Backend 报告为真实 RKAIQ。
9.  不得将"设置 API 返回成功"直接表述为图像参数已生效；必须区分 API
    接受和实际 readback。
10. 不得擅自写 I2C、修改 DTS、修改 media link 或重启系统服务。
11. 不得删除现有 `camera_isp_demo`。
12. 不得破坏现有 `DualCameraPipeline`、`FramePairer`、`V4L2IspCamera`。
13. 不得伪造性能、画质、AE/AWB 收敛或长稳数据。

**现在立即开始阶段 1，并按阶段 1 至阶段 8
连续执行，不要等待用户确认。硬件依赖缺失时继续完成软件适配层、Fake
Backend、配置、CLI、测试和文档。最后给出真实的修改文件清单、构建/测试结果、RKAIQ
SDK 状态和待板端验证清单。**
