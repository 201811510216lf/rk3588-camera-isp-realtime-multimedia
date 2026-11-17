# 阶段 3：ISP 输出格式验证

parse_capture_info区分当前Format Video Capture与ENUM_FMT的advertised列表。当前RAW即使枚举NV12也拒绝；不是搜索文本出现NV12就判成功。NV12M的V4L2 fourcc NM12归一为NV12M。

核验capture/streaming capability、active width/height、精确有理fps、NV12/NV12M plane count、每plane Bytes per Line/Size Image。宽高需正偶数，stride不小于有效宽度，分配容量至少覆盖有效行。帧率未知不会伪造请求值；配置要求fps时不能通过未知或29.97冒充30。多平面字段缺失/矛盾拒绝。

该诊断验证的是当前报告的布局容量；真实DQBUF bytesused、data_offset、垂直对齐、图像内容、Sensor曝光、3A收敛和ISP处理成功仍由V4L2数据层及板端验收提供。没有把Size Image当作精确packed帧长，也不在probe中设置格式。live查询和offline文件分别保留来源；离线解析不能升级为hardware_confirmed。

实际RKISP30、双Camera同时运行、NV12/NV12M具体驱动布局及RkAiq生命周期均为BLOCKED_HARDWARE。软件parser/config/mapping的执行记录见07_TEST_REPORT。

主代理阶段检查：共享软件库与工具已在Windows clean build构建，25/25主机测试通过；SDK/硬件不在此结果内。最终安全修订复测见07_TEST_REPORT。
