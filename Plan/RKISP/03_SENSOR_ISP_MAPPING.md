# 阶段 3：Camera、Sensor 与输出映射

IspConfig.cameras按camera_id显式配置media_device、sensor_entity、expected_isp_entity、video_node。SensorRegistry要求三个不同entity确实出现在同一media图，Sensor function明确报告Sensor，且存在启用的sensor→配置ISP→该video-node所属entity有向路径。只保留证据中的subdev/entity，physical_camera_id与SDK sensor_name不从m00或节点号猜测。

不同logical ID的输出node与media+sensor组合不得重复。跨Camera错误sensor或仅含video节点名称但没有enabled路径不会通过。共享ISP/camera-group资源是否可用由BSP/后端另行确认，图中的路径不等于独立AIQ context授权。

配置parser支持严格、文档化的YAML scalar/list子集：version:1，顶层strict/allow选项，最多两项camera列表（两空格'- camera_id'，四空格字段）。支持引号字符串和注释；拒绝未知/重复键、复杂YAML、负数/溢出、缺少必需映射、非法enum。不是通用YAML库。IN_PROCESS必须显式isp_instance、independent_context_confirmed、BSP start_order/stop_order。文件读取上限1MiB，单行4096；失败保持调用方原配置不变。

configs/isp/isp_config.example.yaml全部硬件身份均为需替换占位符，不能当作真实Sensor声明。sensors.example.yaml说明沿用同一schema，避免重复数据库。tests/fixtures/isp为明确的synthetic parser fixture，只供测试，不构成产品Camera源或板端成功。

主代理阶段检查：共享软件库与工具已在Windows clean build构建，25/25主机测试通过；SDK/硬件不在此结果内。最终安全修订复测见07_TEST_REPORT。
