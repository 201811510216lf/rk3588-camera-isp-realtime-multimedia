# RKISP专项最终交付

已完整阅读任务书并连续执行阶段1–8。保留现有DualCameraPipeline、FramePairer、V4L2IspCamera与camera_isp_demo，优先扩展AiqController/validator/CLI，没有重写视频主链路或新增产品Camera输入。

软件产物：统一结构化状态/配置/结果；只读结构化MediaGraph与Sensor/ISP/output注册；可插拔IRkAiqBackend、外部发现和明确Unavailable backend；AiqController能力/参数/回读、IQ与所有权、RkAiqManager双路回滚/BSP启停phase；默认严格readiness与显式诊断；实际协商绑定、异常清理、ISP健康/首帧stall；CLI/JSON转储、流式NV12/NV12M分析、只读probe与明确抓帧脚本；SDK可选检测/64位目标架构校验/真实头与factory链接probe；Fake仅在测试中。

真实结果：Windows/MSVC clean build+25/25CTest（2.44秒）；MSVC ASan25/25（首次完整运行16.00秒，无报告）；ISP四组各100次，共400次压力（11.68秒）。Python诊断10、CLI4、SDK检测6包含在CTest组内；Shell语法3与参数负例3通过。37个新增/修改C++文件格式化，进行多代理交叉审查。完整记录在07_TEST_REPORT.md，不能以Fake或历史LinuxCI代替板端结果。

关键边界：当前没有真实RKAIQ头/库/示例、IQ、BSP或Camera。真实RockchipRkAiqBackend API端口仍SDK_BACKEND_MISSING/BLOCKED_HARDWARE，框架不会凭空生成厂商函数。ENABLE_RKAIQ=ON需要匹配SDK及核验的RKAIQ_ADAPTER_SOURCE。外部服务无合法IPC时控制UNSUPPORTED；独立CLI不初始化/接管IN_PROCESS会话。实际myDemo厂商路径未在本机编译。Sensor身份、IQ加载、3A生效、画质和24/72h均未测量。

本次按用户授权交付至现有GitHub仓库的main分支，采用普通提交/推送，不强制覆盖远端历史。文件清单见CHANGED_FILES.txt，板端验收见../../docs/RK3588_RKISP_BRINGUP.md，待适配清单见FINAL_HARDWARE_BLOCKERS.md。没有删除模型/录像/独立实验成果，没有改内核、DTS、I2C、media link或系统服务。

自动化复现补充：移除生成目录的ASan运行库后，使用更新的Windows脚本重新clean build并自动复制本机运行库，25/25CTest再次通过，16.87秒。没有依赖手工保留的DLL，也没有上传生成二进制。

本次代码已推送GitHub main，代码提交0017938。其[Linux普通与ASan+UBSan CI](https://github.com/201811510216lf/rk3588-camera-isp-realtime-multimedia/actions/runs/44046706099)均25/25通过，各800次重复压力通过。新增验证是主机Linux证据，不是Rockchip SDK或板端验收。详细耗时见07_TEST_REPORT。
