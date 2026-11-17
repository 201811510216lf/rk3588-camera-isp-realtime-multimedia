# 参考工程结构核对
2026-11-17 只读核对 https://github.com/JoeChen2me/RK-MediaProject 及 master/CMakeLists.txt、AGENTS.md。
CMake 最低 3.16，C++17，src/include/lib/Plan，Threads::Threads，Wall/Wextra/Wpedantic，源码清单显式列出。借鉴模块边界与资源回收，不复制源码/二进制。
参考 USB MJPEG→MPP Decode 不适用于本项目；本项目只有 RKISP NV12/NV12M 两路输入，无 Decode worker。参考工程的本地手册路径也不适用于本机。
