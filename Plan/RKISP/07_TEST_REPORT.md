# 阶段7：构建、测试与审查

读取阶段1–6报告与最后代码。实际环境Windows/MSVC19.44、CMake3.31.6/Ninja，RKAIQ默认OFF。最终格式化版本clean build，25/25CTest通过，2.44秒。包含原18组回归，以及新发现/配置、Fake生命周期、readiness、health、CLI、Python诊断与SDK检测。

新增ISP四组（discovery/lifecycle/readiness/health）各重复100次，共400次通过，11.68秒。Python诊断10/10、CLI4/4、SDK配置6/6（均包含在CTest组内）；Shell语法3/3及抓帧参数负例3/3通过。37个修改/新增C++文件clang-format并dry-run核验，完成多代理交叉审查。

新增standalone ENABLE_ASAN支持MSVC，本机ASan编译成功。最初运行0xc0000135是Windows未从工具链PATH找到ASan动态运行库，复制本机已安装运行库到被忽略的build目录后25/25通过，16.00秒，无ASan报告；Windows构建脚本已自动执行该步骤。不是RKAIQ SDK二进制，不提交生成DLL。UBSan/TSan在当前MSVC未运行；旧LinuxCI结果不算本轮验证。

测试覆盖：enabled sensor/ISP/output路径、伪ENABLED/错误pad/RAW与advertisedNV12混淆、NV12/NV12M stride/plane/FPS、重复/未知配置、IQ缺失/越界/失配、未知或冲突所有权、normal/失败init/prepare/start/stop回滚、双路部分失败、明确BSP启停phase、after-stream失败延迟清理、running禁止deinit、Fake READY屏蔽、UNAVAILABLE不查询backend、有限参数/能力/readback不匹配、已接受请求与未确认硬件区别、只读CLI非零/JSON/保护转储、离线来源隔离、零首帧stall/服务过期/sequence时钟异常、SDK缺失/架构/ELFclass/未知arch/未检查archive/无适配source拒绝。

中间失败已修复：Windows中文fixture路径、旧对象与中途schema变化、CMake错误输出UTF8解码/自动换行、backend_info局部遮蔽。最终结果来自最后clean build，不沿用旧二进制。编译保留少量既有实验算法的数值转换警告，不声称零警告。

边界：本地主机完整构建包含ISP软件核心、图解析、状态机、CLI及Fake测试，不包含依赖RGA/MPP/RKNN的myDemo。Pipeline接线经静态审查及独立readiness/binding测试，不能宣称厂商目标已编译。没有真实RKAIQ头/库/adapter、ISP/Sensor/IQ或/dev节点，真实SDK构建与板端运行BLOCKED_HARDWARE。

自动化复现补充：移除生成目录的ASan运行库后，使用更新的Windows脚本重新clean build并自动复制本机运行库，25/25CTest再次通过，16.87秒。没有依赖手工保留的DLL，也没有上传生成二进制。
