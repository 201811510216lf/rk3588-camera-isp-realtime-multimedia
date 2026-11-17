# 阶段1：ISP重新审计（2026-11-17）
实际基线main=55e112d，工作树干净。已读AGENTS、当前BSP/架构、AiqController/CLI、MediaGraph validator、V4L2和Pipeline启动路径、三个采集脚本、CMake/测试。
与任务书起点差异：main入口已迁至src/main.cpp，实现在src而非lib；已有独立录像模式、行为事件/健康监控，必须增量接入而不重写。当前AiqController仍DISCOVERY_ONLY；ENABLE_RKAIQ一律配置失败。
关键缺口：IQ根目录递归文件名搜索与runtime概念混淆；没有sensor/ISP/output映射；进程名发现不等于同Camera所有权或加载证据；控制均Unsupported；CLI数字/多控制解析不严格；图校验仅检查非空与NV12子串，未证明enabled route，默认media0不能作为设备依据。
V4L2帧plane/stride/bytesused/lease、独立Pipeline/队列保留。media-ctl/v4l2-ctl查询只读，未发现配置link/control或I2C；probe不变更系统，显式capture脚本才STREAMON。
主机基线：Windows Ninja/MSVC clean build，18/18CTest通过。没有RKAIQ环境变量、目标SDK头库、BSP/内核/Sensor资料或Linux设备证据；真实运行BLOCKED_HARDWARE。测试不代替板测。
