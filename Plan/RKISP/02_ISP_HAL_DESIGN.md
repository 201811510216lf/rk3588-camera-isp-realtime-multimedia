# 阶段2：ISP HAL和配置模型

已读阶段1审计、API差距和Camera映射报告。复用已有AiqController，不另外创建同名控制器；include/isp/rkaiq_controller.h只提供管理器，旧include/aiq_controller.h保留兼容控制方法。

统一isp_types定义逻辑Camera ID、SensorIdentity、状态、所有权、工作模式、能力、可选控制回读、IQ确认状态和结构化IspControlResult。结果区分request_accepted与hardware_confirmed；backend名/版本、操作、Camera、错误、来源均可序列化。未知值不填测量结果。Fake后端不能提升真实硬件ready或确认IQ加载。

AiqCameraConfig配置sensor/media/output与IQ根/文件、BSP资源标识和显式BeforeStream/AfterStream及BeforeStreamOff/AfterStreamOff顺序；默认ExternalService，默认严格readiness。采集类仍只负责V4L2，不包含AIQ初始化。graph/registry由独立接口提供证据，离线图不成为实时ready。

IRkAiqBackend隔离info、initialize/prepare/start/stop/deinitialize、能力/状态/控制回读、IQ身份验证和AE/AWB/曝光/增益。真实工厂只连接经目标SDK核验的adapter，缺SDK返回BACKEND_UNAVAILABLE。配置parser、图、readiness/health复用这些类型，不猜SDK函数。

软件实现与测试逐阶段增量编译；本阶段新增接口/实现的真实测试结果由主代理阶段检查和最终07_TEST_REPORT记录，不提前标记SDK或硬件通过。

主代理阶段检查：共享软件库与工具已在Windows clean build构建，25/25主机测试通过；SDK/硬件不在此结果内。最终安全修订复测见07_TEST_REPORT。
