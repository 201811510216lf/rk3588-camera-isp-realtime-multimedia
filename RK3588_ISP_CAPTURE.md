# RKISP NV12/NV12M采集

## Linux最小构建

```bash
cmake -S . -B build-capture -DBUILD_RK_DEMO=OFF -DENABLE_V4L2=ON -DBUILD_BSP_TOOLS=ON
cmake --build build-capture --parallel
ctest --test-dir build-capture --output-on-failure
```

该目标编译真实Linux V4L2采集源，不要求RGA/MPP/RKNN SDK。不是完整厂商myDemo编译或开发板验收。Windows主机构建不包含Linux ioctl目标。

## 节点与配置

从实际media-ctl拓扑填写[配置示例](configs/isp/isp_config.example.yaml)中的media_device、sensor_entity、expected_isp_entity、video_node及每路width/height/fps/pixel_format。节点编号没有默认值，不以video数字推断Camera身份。用实际发现的路径替换下面占位符：

```bash
media-ctl -d /dev/MEDIA_FROM_DISCOVERY -p
v4l2-ctl -d /dev/ISP_OUTPUT_FROM_DISCOVERY --all --list-formats-ext
./build-capture/media_graph_validator --config /data/isp.yaml --json
```

独立validator检查当前活动格式，协商前与目标不同会报告差异；主采集流程在格式协商后重新核验。RAW节点、错误Sensor/启用链路、IQ/所有权冲突不能用诊断开关绕过。外部服务、IQ和3A必须单独验证，不能从NV12枚举或抓到图像推断。

## 单路有界抓帧

先确认BSP已有IspServer或提供已核验的in-process SDK port。父目录须存在，输出目录必须为新目录；已有证据不会覆盖：

```bash
./build-capture/isp_capture --isp-config /data/isp.yaml --camera-id 0 --out-dir /data/cam0-first --frames 120 --timeout-sec 60
./build-capture/isp_capture --isp-config /data/isp.yaml --camera-id 1 --out-dir /data/cam1-first --frames 120 --timeout-sec 60
```

默认遵循配置的严格readiness。没有合法3A状态查询但已人工完成BSP核验时，可明确追加--allow-unverified-isp做诊断，结果仍保留UNVERIFIED_ISP；这不算ISP/3A验收通过。工具只选择指定一路，不顺带初始化另一Camera。

生成capture.nv12（去除每行padding的紧凑NV12；NV12M也按Y/UV两平面打包）与capture.jsonl。逐帧记录原Camera/index/sequence、源时间戳与clock domain、出队观测、flags、planes、stride、offset、payload、sizeimage，以及输出文件偏移。session记录G_FMT实际尺寸/格式/field及G_PARM原始帧间隔；0表示未知。end记录完整性和关闭结果。超时、信号、ioctl/写入/关闭失败非零退出，保留已有部分文件。

查看前从session提取实际宽高；播放FPS为frame_interval_denominator/frame_interval_numerator，未知时由板端测量，不能用请求帧率伪造：

```bash
# WIDTH、HEIGHT、FPS来自本次capture.jsonl，不是硬编码的板端参数。
ffplay -f rawvideo -pixel_format nv12 -video_size "${WIDTH}x${HEIGHT}" -framerate "$FPS" /data/cam0-first/capture.nv12
```

## Buffer与IO模式

QUERYCAP/ENUM_FMT → S_FMT/G_FMT → S_PARM/G_PARM → REQBUFS(MMAP) → QUERYBUF/mmap → QBUF → STREAMON → poll/DQBUF → 最后消费者释放lease → QBUF。平面数据指针包含data_offset，bytesused只表示偏移后的有效数据，不把mmap容量当payload。最后一行无需包含未使用的尾部padding。

V4L2_MEMORY_DMABUF导入/外部分配器本轮没有实现，不会无条件选择该模式。MMAP所有权按DQBUF/QBUF交接；如果未来将DMA FD另行映射为CPU访问，须依据目标分配器使用DMA_BUF_IOCTL_SYNC并验证缓存一致性。本轮没有用未核验SDK函数伪装cache同步。

独立脚本scripts/isp_probe.sh保留只读诊断；scripts/isp_capture_experiment.sh仍提供v4l2-ctl的显式有界抓帧。优先使用isp_capture验证本项目的正式Camera类与plane适配。
