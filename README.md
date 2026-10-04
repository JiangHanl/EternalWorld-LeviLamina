[简体中文](README.md) | [English](README_EN.md)

# EternalWorld · LeviLamina

以 Minecraft Bedrock Dedicated Server 与 LeviLamina 为运行环境的原生 C++ 服务器工程。目标架构是一个薄加载插件 **EternalHost**、八个内部原生模块，以及面向开发者的 EternalSDK C/C++ 接口。Host 接触引擎；模块按业务职责拆分，通过版本化接口协作。

目前是开发原型。旧 Phase 1 的最小 Core DLL 已完成真实 BDS 加载与生命周期验证，独立 SQLite 事务域通过 17 组测试；新的 Host 架构属于 Phase 1.5，只有实际构建、测试和运行证据完成后才记为通过。玩家资产 API 保持关闭，尚不能作为完整生存服务器部署。

| 入口 | 内容 |
|---|---|
| [当前状态](CURRENT_STATUS.md) / [测试报告](TEST_REPORT.md) | 已验证范围与未完成项 |
| [架构](ARCHITECTURE.md) / [路线图](ROADMAP.md) | Host 与八模块边界、实施门槛 |
| [构建](BUILD.md) / [依赖](DEPENDENCY.md) | 固定工具链、构建和版本回归 |
| [API](API.md) / [ABI](ABI.md) / [模块开发](MODULE_DEVELOPMENT.md) | C ABI 与 C++ 辅助接口 |
| [数据](DATABASE.md) / [迁移](MIGRATION.md) | 唯一资产权威、恢复边界 |
| [贡献](CONTRIBUTING.md) / [安全](SECURITY.md) | 开发约束与问题报告 |

源码、运行文件和私人运营数据分离。仓库不提供 BDS、LeviLamina 二进制、世界、玩家数据库、运营者身份、密钥或原版资源。依赖版本与来源记录在工具链清单；公开包只包含经审查的自研产物、配置示例和许可证。

自主实现采用 [MIT](LICENSE)。上游依赖不因此变更许可证，详见 [第三方声明](THIRD_PARTY_NOTICES.md)。本项目与 Mojang、Microsoft 和 LeviMC 无隶属关系。
