# 阶段 1：仓库审计（2026-11-17）
已读任务书、agent.md、README、CURRENT_ARCHITECTURE、CMake、源码/测试清单；初始 main=23448f3，工作树干净。
当前 src 实现在 lib，myDemo 是真实双 Camera 入口；camera_isp_demo 是独立 RAW10 实验。保留复用，迁移实现到 src，不复制外部实现。
问题：sink 同步 fwrite/pclose 可无限阻塞；配帧统计样本无界增长；队列只有 drop-oldest；默认设备硬编码；缺行为事件、录像关联和运行监控。既有 CameraFrame lease 支持共享最终归还，V4L2 已有 Linux 隔离。
清理范围：被替代的旧执行计划、重复审查/面试资料、合成 reports 和生成日志；保留实验代码、配置、BSP/ISP 文档与工具。删除可由 Git 历史恢复。不删除模型、用户采集视频或外部文件。
硬件/SDK：当前 Windows 主机，没有 RK3588/真实双 Camera 或 SDK 证据，全部标记 BLOCKED_HARDWARE。
