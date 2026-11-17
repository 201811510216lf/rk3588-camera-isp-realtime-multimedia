# 项目技术说明要点

- 两路输入为什么是RKISP NV12：RAW去马赛克/3A由硬件ISP与RkAiq完成，应用读取处理后输出；Software ISP是独立学习实验。
- 怎么避免提前QBUF：DQBUF形成共享lease，最后使用者归还；关闭先停止生产、消费者释放后才解除映射。
- 怎么处理NV12M：两个storage plane分别保留stride/payload/offset，不把width*height*1.5当驱动buffer大小。
- 配帧不等于曝光同步：只比较同域时间戳，hold-last标stale；独立录像不等待配对。
- 如何隔离慢AI和断网：独立有界队列；AI可丢旧帧，网络丢GOP后等IDR；录像不足显式失败，不能隐藏实验帧丢失。
- 如何保证模型语义：sidecar+SDK属性校验、preprocess/postprocess schema；检测/跟踪/行为/时间状态机分开，没有行为映射不生成结果。
- 怎么关联事件录像：Camera/sequence/source timestamp对应实际接受包，再核对finalized分段与offset；跨段保留多个span，缺失标记partial。
- 如何说明真实边界：主机测试、SDK编译、真实Camera、模型精度和24/72h是不同证据。MPP EOS/PTS和VFR仍需适配，不把接口或模拟当硬件完成。
