# 阶段 9：测试策略

读取阶段 8 事件与运行管理报告后执行统一 clean build，避免旧 MSVC 本地化 showIncludes 依赖记录造成旧对象残留。Windows 脚本每次 clean-first；C++ assert 在测试目标保持启用。

测试层：布局/lease与回收→四队列策略/关闭/压力→同域配帧→模型契约/CPU张量→tracking/事件状态机→编码包时间基/独立sink→多模块内存fixture集成→最终录像索引关联。Fixture 不进入产品运行模式。真实视频编码和模型精度不属于这些测试。

重复压力使用 CTest --repeat until-fail:100。ASan+UBSan 与 TSan 分别用独立目录，Linux GCC/Clang 开关支持；当前 Windows MSVC 对组合 sanitizer 明确拒绝。Linux CI 负责主机可移植性和ASan+UBSan，未运行的远程检查不得提前记为通过。

SDK负例检查：开启V4L2于Windows、RGA/MPP/RKNN依赖缺失、应用侧RkAiq未实现时，配置必须明确失败。硬件列表：双真实节点、ISP/3A、RGA/MMAP/DMA完成、MPP时间轴/EOS、RKNN张量对照、ffprobe与网络/磁盘故障、24/72h都须板端实测。
