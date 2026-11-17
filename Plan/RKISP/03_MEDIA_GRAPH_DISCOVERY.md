# 阶段 3：结构化 Media Graph 发现

已读取阶段1审计和阶段2 HAL设计，复用原media_graph_validator入口并将其实现统一到media_graph_inspector/SensorRegistry，不创建另一套substring检查。

实现解析原始media-ctl -p文本：media设备、driver/model/bus info、entity ID/name/function、subdev/video node、pad index/direction及可用bus format/尺寸、定向link enabled/immutable。去重双向打印的同一link，拒绝冲突、缺失entity/pad、方向错误、重复ID/节点以及超出界限的文本。最多4MiB诊断文本、1024 entity、64 pad/entity、8192 link。

只读Inspector的Linux live模式通过固定argv执行media-ctl -p -d和v4l2-ctl --all --list-formats-ext；无shell展开，输出及3秒采集期限有界。未提供media device时失败，不默认media0。Windows live返回BLOCKED_HARDWARE。离线文件明确标记OfflineFile，parser本身默认NotMeasured，不能由解析成功得到live ready。

没有MEDIA_IOC_SETUP_LINK、subdev S_FMT、V4L2 control、STREAMON、I2C或服务启停操作。ISP generation仅在原始driver/model明确为rkisp30时标记RKISP30_REPORTED；泛化rkisp名称仍UNKNOWN，配置期望也不是版本证据。

新增isp_discovery_test和替换旧validator测试fixture，覆盖重复/悬空/错误方向/关闭link、字段提取及offline证据。本阶段编译/执行结果由主代理的阶段检查及07_TEST_REPORT记录，未提前宣称通过。真实板端发现仍BLOCKED_HARDWARE。

主代理阶段检查：共享软件库与工具已在Windows clean build构建，25/25主机测试通过；SDK/硬件不在此结果内。最终安全修订复测见07_TEST_REPORT。
