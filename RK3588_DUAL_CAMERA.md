# RK3588双路Camera接入现有Pipeline

## 每路配置与数据适配

Camera 0/1各自从ISP配置读取media_device、sensor_entity、expected_isp_entity、video_node、width、height、fps、pixel_format。主程序允许只传--isp-config；显式--camera0/1、--input-width/height、--fps或--pixel-format与文件冲突会报错，避免暗中改变已验证映射。无文件的原CLI入口保留，但没有可信ISP证据不能获得严格就绪。

两路V4L2IspCamera独立开流/采集，CameraFrame保存实际协商布局、源sequence/时间戳和共享lease。现有有界录像/AI队列与FramePairer继续使用；不会在消费者未完成时QBUF。源格式NV12M无需改变RKNN/RGA/MPP接口，由既有CPU打包函数生成连续NV12。

Independent模式支持两路不同配置尺寸/FPS，按实际frame.desc读取，分别设置输出CFR时基；Joint模式继续复用原合成布局，要求两路尺寸与实际有理数帧间隔相符。软件FramePairer不代表同步曝光，硬件同步与共享ISP时分能力必须由板端证实。

## 启停

只读Sensor/ISP/node映射与所有权/IQ准备 → BSP声明的RkAiq前置阶段 → 各路V4L2 S_FMT/G_FMT与Buffer准备 → STREAMON → BSP后置阶段 → 重新检查实际输出/readiness → 原Capture/AI/录像线程。

Capture线程不初始化RkAiq。关闭先停止采集生产者、关闭并清空各队列/FramePairer、等待消费者归还lease，再按BSP声明在STREAMOFF前后停止所属AIQ并释放Camera资源。外部服务不由应用停止。Independent单路失败会先清理该路再继续存活路；Joint无法获得成对输入会失败，不能伪装正常。

## 板端执行顺序

1. 核验IspServer/或真实SDK port、Sensor/IQ/AE/AWB。
2. 用实际media graph确认CIF/ISP/mainpath或selfpath到输出节点的启用链路。
3. 按[采集说明](RK3588_ISP_CAPTURE.md)先各做单路真实NV12抓帧和查看。
4. 验证两路同时开流及ISP资源调度能力，再运行原myDemo。
5. 先关闭AI验证双路录像，再增加真实模型/RKNN，检查MPP H.264和FFmpeg输出。

目标BSP构建完整myDemo仍须现有RGA、MPP、RKNN、OpenCV及匹配Linux工具链，见[README](README.md)。示例使用填好真实节点的配置：

```bash
./build-rk/myDemo --isp-config /data/isp.yaml --video-mode independent --no-ai --record --output-dir /data/dual-first --no-stream --duration-sec 60
```

这只是待板端执行命令，当前没有实际图像、模型、H.264、双路长稳结论。SDK编译和板端实测仍BLOCKED_HARDWARE，见[测试报告](RK3588_ISP_TEST_REPORT.md)。
