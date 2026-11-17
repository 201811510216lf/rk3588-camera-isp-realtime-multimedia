# 阶段 9：真实测试结果

环境：2026-11-17，Windows，Visual Studio 2022 MSVC19.44，CMake3.31.6，Ninja，Debug。没有RK3588、SDK、真实Camera或大鼠行为模型。

| 检查 | 实际结果 |
|---|---|
| scripts/build_host_windows.ps1，clean-first | 构建成功，核心/独立实验/BSP工具/全部主机测试 |
| CTest --output-on-failure | 18/18通过，最终格式化后版本耗时1.25秒 |
| runtime_monitor/camera_queue/behavior_event/output_pipeline，各repeat100 | 全部通过，400次执行，25.05秒 |
| Python事件录像关联 | 5/5通过（另被CTest第18组调用） |
| SDK/平台负例 | V4L2于Windows、RGA/MPP/RKNN缺依赖、RkAiq未实现、组合sanitizer不支持，6项均按预期明确配置失败 |
| compile_commands.json | Ninja实际生成，未上传生成文件 |
| clang-format | 37个新/实质修改C++文件格式化并核验；纯迁移的实验算法保留 |
| Software ISP独立smoke | 64x48、两VirtualSensor、每路2帧，退出成功；simulated=true，不是硬件数据 |
| git diff --check | 通过 |

主机覆盖：NV12/NV12M/stride/bytesused、lease最后释放和重复归还、四队列策略/timeout/关闭唤醒、多生产者压力、配帧漂移/掉帧/时钟/滚动分位数、严格模型契约与CPU张量、Tracking/时间事件/20k帧序列、独立sink阻塞/溢出/GOP恢复/重复关闭、时间基溢出、packet源索引、完整软件集成和多分段事件关联。

早期问题：旧VS生成器访问用户SDK目录被拒绝；已改用Ninja与显式SDK路径。初次迁移结构检查仍引用lib，已更新；一次FramePairer对象依赖记录残留导致失败，clean-first重建后消失。所有最终结果来自本轮完整重编译，没有沿用build-review-msvc旧二进制。C++集成fixture的显式构造编译错误已修正并重测。

构建保留部分原有数值转换/未使用参数警告（Software ISP/CompositeProcessor），没有将其报告为零警告构建。clang-tidy/独立静态分析工具未运行；完成多代理人工源码审查。Windows组合ASan/UBSan和TSan不可用；Linux自动化提供单独构建入口，远程结果需另列，不预先声称通过。

BLOCKED_HARDWARE：真实V4L2/RKISP/RkAiq、厂商SDK编译、RGA/MPP/RKNN、真实MP4/服务器/ffprobe、模型精度、24/72h板测均未执行。源码中的硬件适配不等于硬件功能已验证。

## GitHub Linux 远程验证

代码提交5d92bef的[GitHub Actions运行](https://github.com/201811510216lf/rk3588-camera-isp-realtime-multimedia/actions/runs/44039654482)已完成并结论success。GNU15.2，ENABLE_V4L2=ON，使用真实Linux系统头编译了V4L2采集代码与Linux子进程实现（没有Camera设备或SDK）。

| 作业 | 普通CTest | 每组100次压力（4组共400次） |
|---|---|---|
| Linux普通 | 18/18通过，0.37秒 | 全部通过，10.82秒 |
| Linux ASan+UBSan | 18/18通过，1.55秒 | 全部通过，97.35秒 |

没有sanitizer报告。TSan未执行；不能把ASan/UBSan覆盖写成TSan结论。Linux宿主编译仍不包含myDemo的厂商SDK路径，也没有执行真实FFmpeg网络/MP4或硬件验收。该验证后仅更新交付文档，未改动已通过检查的源码。
