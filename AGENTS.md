# 项目协作约定

主产品只有两路真实 RKISP NV12/NV12M Camera；VirtualSensor/RAW10/Software ISP 保留为独立实验，禁止接入主链路或删除其代码。

- C++17，CMake >=3.16，公共接口 include/，实现 src/，实验入口 tools/，设计和状态 Plan/。
- 采用 .clang-format；使用 Threads::Threads 和明确的有界队列/lease 所有权。
- SDK 选项开启而依赖缺失必须失败；不提交 BSP SDK、第三方二进制、模型、实验录像或运行日志。
- Linux：bash scripts/run_tests.sh；Windows：powershell -File scripts/build_host_windows.ps1。压力测试：bash scripts/run_stress_tests.sh。
- 主机测试帧只是 fixture，不能增加文件/USB/RTSP/Mock 产品输入源。
- 验证分别记录软件单元测试、SDK 编译、真实双 Camera、模型部署、24/72h 板测；缺设备标记 BLOCKED_HARDWARE。
- 目标检测与行为识别分开；无真实行为模型不得生成行为事件或准确率。模型预处理/输出配置必须匹配 metadata。
- 默认不自动删除实验原始数据；修改清理范围写入 Plan/CLEANUP_MANIFEST.txt。
- 上传前检查 diff、构建和测试；不得使用旧二进制的结果替代本轮源码验证。
