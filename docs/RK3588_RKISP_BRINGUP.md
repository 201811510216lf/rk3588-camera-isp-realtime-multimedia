# RK3588 RKISP30 / RkAiq 板端验收

本手册为待执行步骤，当前没有BSP、Sensor资料、RKAIQ SDK或硬件证据，全部实机项为BLOCKED_HARDWARE。占位符必须用实际发现值替换。

1. 记录官方BSP/kernel commit、架构/工具链、RKISP/RKCIF驱动和SDK版本。不要仅凭generic rkisp名称断言RKISP30。
2. 确认真实Sensor驱动与DTS compatible/供电/时钟/端点；本仓库不生成未知Sensor寄存器、I2C/GPIO/lane或内核补丁。
3. 保存只读原始拓扑与设备枚举：

```bash
bash scripts/isp_probe.sh > /data/evidence/probe.txt
media-ctl -p -d /dev/MEDIA_FROM_DISCOVERY > /data/evidence/media.txt
v4l2-ctl --list-devices > /data/evidence/devices.txt
v4l2-ctl -d /dev/ISP_OUTPUT_FROM_DISCOVERY --all --list-formats-ext > /data/evidence/output.txt
```

输出目录须预先创建；probe只读，不能配置link、control或重启服务。media_graph_validator的live查询使用固定argv、限时和有界输出；离线证据仅OFFLINE_FILE。

4. 从启用的Sensor→CSI/CIF→指定ISP→processed video path核验两路映射。RAW节点不能作为NV12输出；logical0/1不能推断physical id。
5. 复制configs/isp/isp_config.example.yaml，逐项填写media/sensor/ISP/video、尺寸/FPS/格式、实际IQ文件与BSP资源。严格YAML子集不支持任意anchor/flow/多行结构，未知键拒绝。
6. 运行media_graph_validator --config /data/isp.yaml。核对active格式与advertised格式、plane count/stride/sizeimage/FPS；offline结构ok不等于live ready。
7. 只读检查systemd、IspServer/daemon进程与逐Camera归属。默认EXTERNAL_SERVICE不init/start/stop已有上下文。候选进程不能证明IQ已加载；无合法IPC时控制UNSUPPORTED。
8. 核对IQ文件可读、canonical路径位于iq_root内，真实SDK返回Sensor身份/版本兼容和实际loaded。名称匹配或目录存在不能填CONFIRMED。
9. IN_PROCESS只有确认无外部owner且SDK支持时使用。按目标BSP填写start_order: before-stream|after-stream、stop_order: before-streamoff|after-streamoff，说明独立context/共享ISP资源；不要因双路盲目建两个context。合作式锁补充而不能代替厂商所有权协议。
10. 将真实SDK port source映射到IRkAiqBackend，逐项核对实际声明/返回码/线程约束。示例构建（不是已执行命令）：

```bash
cmake -S . -B build-rkaiq -DENABLE_RKAIQ=ON \
  -DRKAIQ_ROOT=/opt/ACTUAL_BSP/rkaiq \
  -DRKAIQ_INCLUDE_DIR=/opt/ACTUAL_BSP/rkaiq/ACTUAL_INCLUDE \
  -DRKAIQ_LIBRARIES=/opt/ACTUAL_BSP/rkaiq/ACTUAL_LIB/librkaiq.so \
  -DRKAIQ_ADAPTER_SOURCE=/opt/ACTUAL_BSP/verified_rkaiq_port.cpp
cmake --build build-rkaiq --parallel
```

当前没有该port，ENABLE_RKAIQ=ON不会凭空生成厂商API实现。rkaiq_sdk_link_probe编译真实头并链接factory，只建立编译/链接证据。

11. isp_control_cli --isp-status --json --isp-config /data/isp.yaml --camera-id 0；查看SDK/平台来源、ownership、IQ状态、AE/AWB/exposure/gain回读。无IPC的external backend只发现；独立CLI不擅自初始化IN_PROCESS，不能接管运行中的Pipeline。设置API必须在真正拥有的running会话或已核验IPC中完成，检查request_accepted与hardware_confirmed。
12. 显式有界单路采集：bash scripts/isp_capture_experiment.sh --device /dev/ISP_OUTPUT_FROM_DISCOVERY --out-dir /data/evidence --frames 120；核对真实DQBUF width/height/stride/plane/bytesused/timestamp/sequence。脚本抓帧输出不能编造曝光时间。
13. 先单路持续采集，再双路同时采集。应用--isp-config配置与--camera0/1及尺寸/FPS必须一致；协商不一致立即失败。默认严格就绪。只有显式--allow-unverified-isp可用于未验证诊断，不能作为ready验收。
14. 接入现有AI/MPP流水线，核对真实上下文与回收顺序、AI结果帧关联、录像源索引。RKISP的NV12不进入Software ISP RAW处理。记录FPS/drop/latency/CPU/RSS与isp-health.jsonl、服务/首帧stall、AE/AWB/IQ状态；真实图像分析仅简单亮度/噪声代理，不是IQ标定。
15. 24h通过后再做72h：固定硬件/模型/配置与合格阈值，包含断网、AI慢、单路故障、停止/重复启停、磁盘门槛、日志轮转；保存原始数据、trace和失败报告。当前自动Camera reconnect为NOT_IMPLEMENTED，未宣称自动恢复或硬件同步/零拷贝。
