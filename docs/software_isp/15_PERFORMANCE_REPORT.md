# Software ISP 性能记录范围

当前 demo 的 `measured_fps = total / elapsed` 是无实时节流情况下每路迭代次数除以墙钟时间；两路模式串行执行两次处理，不能把该数值写成单模块基准、真实相机采集 FPS 或 RK3588 性能。

`--fps` 决定虚拟帧数及时间戳步长；`realtime_pacing=false`。`--duration` 决定虚拟帧数，不承诺执行恰好对应的墙钟时长。

仓库 `reports/isp` 由 `scripts/run_round6_reports.py` 产生的图像、曲线和 JSON 是独立辅助合成结果，不是 C++ 性能/收敛实测。真实分辨率、算法配置、编译模式、硬件、墙钟时间、每路/总帧数和输出 I/O 成本应与将来的测量一起记录。

本轮没有新增软件 demo 吞吐测量。已执行的主机检查及未复验范围见 [CODEX_EXECUTION_STATUS.md](../CODEX_EXECUTION_STATUS.md)；4K@30、板端延迟、RGA/MPP/RKNN 和长稳仍未验证。
