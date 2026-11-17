# 阶段4：目标SDK API兼容边界

当前环境未提供RK3588 BSP对应RKAIQ headers/libraries/examples。任务书列出的enumStaticMetas/getBindedSnsEntNmByVd/preInit/init/prepare/start/stop/deinit只是检索线索，未从网络版本复制或猜测参数/返回码/线程约束。

IRkAiqBackend是本项目C++适配接口，不是Rockchip ABI。ENABLE_RKAIQ默认OFF，此时create_rockchip_backend返回明确BACKEND_UNAVAILABLE。ON必须指定实际头文件/SDK库及经审查的RKAIQ_ADAPTER_SOURCE，该source实现isp::create_sdk_backend；系统头编译、目标架构检查与工厂链接probe由CMake接入。不能仅库文件存在就声称API或硬件验证。

后续实际SDK接入须逐项记录：BSP/kernel/SDK提交与版本查询机制、ISP generation证据、Sensor entity/physical ID关联、Normal/HDR参数结构、SDK规定的prepare/start与V4L2顺序、错误返回和部分初始化释放、AE/AWB/曝光/gain有效范围/单位/回读、IQ身份/版本/加载查询、资源所有权/线程约束、双独立context或camera-group要求。无法查询版本填UNKNOWN，不从文件名推断。

真正adapter必须只在可用ctx状态调用vendor API，initialize失败自行处理其SDK部分资源，stop失败不能在析构直接deinit运行context；本控制层会继续保守回滚并报告失败。外部service只有已记录合法IPC时才能提供控制，当前发现backend全部UNSUPPORTED。

状态明确区分：主机HAL/状态机已实现；Fake用于契约测试；真实SDK编译BLOCKED_HARDWARE；真实RKISP30/RkAiq板端运行BLOCKED_HARDWARE。

主代理阶段检查：共享软件库与工具已在Windows clean build构建，25/25主机测试通过；SDK/硬件不在此结果内。最终安全修订复测见07_TEST_REPORT。
