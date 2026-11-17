# 阶段 2：结构与构建
读取阶段 1 报告后，将 lib/*.cpp 最小迁移到 src，保持 include 和兼容目标名。CMake 3.16/C++17/Threads::Threads/编译命令导出；SDK 独立选项缺失即明确报错，不伪造成功。Windows Ninja/MSVC 构建避免旧 VS 生成器读取受限的用户 SDK 目录。旧 build-review-msvc 构建失败：SDK 路径访问被拒绝；其中预先存在的测试二进制不计作本轮验证。
