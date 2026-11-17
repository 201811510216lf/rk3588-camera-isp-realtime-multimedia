# Camera标识映射基线
已有camera_id=0/1只是应用逻辑标识，设备节点由CLI指定；没有可靠Sensor entity、media device或RKAIQ物理ID证据。不得由数组序号推断physical camera id，也不得将video数字当camera id。
新增配置驱动logical id→sensor entity→media device→expected ISP entity→processed output。只用启用的有向拓扑路径建立映射；离线保存证据可测试parser，不能令主业务默认READY。Camera group必须留给匹配SDK/BSP适配。
