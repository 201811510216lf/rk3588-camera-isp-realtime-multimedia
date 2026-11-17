# IMX415 Sensor Driver 分析与适配边界

## 原理

Linux sensor driver 通过 I2C 配置寄存器，以 V4L2 subdev 向 CSI/ISP 暴露 media-bus code、帧尺寸、帧间隔和 controls。IMX415 的寄存器地址、初始化顺序、RAW10/RAW12 packing、曝光/增益转换必须来自目标 BSP 或 Sony/模组厂商资料，不能从其他 Sensor 猜测。

## 当前 BSP 依据

仓库没有 kernel tree 或 `drivers/media/i2c/imx415.c`。用户态源码只在 `src/v4l2_isp_camera.cpp` 消费 ISP 输出 NV12；它不是 Sensor driver。官方/板商驱动应优先复用，适配以最小 diff 方式提交。

可追溯的上游参考是 [Linux `imx415.c`](https://linux.googlesource.com/linux/kernel/git/tip/tip/+/refs/heads/perf/urgent/drivers/media/i2c/imx415.c)；其寄存器和 mode 不能直接套用于未知 Rockchip vendor BSP。

## 关键源码路径

- 现有用户态 V4L2：`demo_multhread_decode_infer_mulmodel/lib/v4l2_isp_camera.cpp`。
- 期望内核路径：`drivers/media/i2c/imx415.c`、`drivers/media/platform/rockchip/isp/`（以实际 BSP 为准）。
- 需要审查函数：`probe/remove`、`s_power` 或 runtime PM、`set_fmt/get_fmt`、`enum_mbus_code/frame_size/frame_interval`、`s_stream`、`v4l2_ctrl_handler`、async subdev registration。

## 适配检查表

I2C chip ID、power-on rollback、xclk/reset/pwdn、RAW10/RAW12 code、3840×2160 mode table、frame/line length、VBlank/HBlank、exposure/gain range、stream on/off、runtime suspend/resume、错误回滚和资源释放。

## 调试命令

```bash
dmesg | grep -Ei 'imx415|i2c|csi|rkcif|rkisp'
v4l2-ctl -d /dev/v4l-subdevX --all
media-ctl -p -d /dev/media0
```

## 测试结果与状态

SOURCE_IMPLEMENTED：分析清单；BOARD_VERIFIED：NOT_TESTED；NOT_IMPLEMENTED：本仓库未写入伪造寄存器或 kernel 驱动；BLOCKED_BY_HARDWARE：缺真实 BSP 和 mode table。

## 面试要点

说明为什么不能只改 compatible：sensor mode、lane/link frequency、pixel rate 和 ISP media graph 必须成套验证。
