[简体中文](README.md) | [English](README_EN.md)

# EternalWorld · LeviLamina

以 Minecraft Bedrock Dedicated Server 与 LeviLamina 为运行环境的原生 C++ 服务器工程。目标架构是一个薄加载插件 **EternalHost**、八个内部原生模块，以及面向开发者的 EternalSDK C/C++ 接口。Host 接触引擎；模块按业务职责拆分，通过版本化接口协作。

目前是 Phase 1.5 开发原型。本地九个 DLL、23 组 Host 测试、17 组 SQLite 事务域测试及真实 BDS 双启动、停用恢复和完整停服已通过；云构建仍待独立验证。玩家资产 API 保持关闭，七个业务模块仅有合法骨架，不能作为完整生存服务器部署。

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

## 运行架构与模块

```text
BDS + LeviLamina
└─ plugins/Eternal/
   ├─ manifest.json → EternalHost.dll
   ├─ modules/ → 八个独立 DLL
   └─ config/ data/ logs/ resources/
```

Host 只负责发现、依赖解析、ABI/能力检查、生命周期、Service Registry、EventBus 与错误隔离。内部 DLL 不是独立 LeviLamina NativeMod。最终运行不依赖 YEssential、LSE、LegacyMoney、LegacyRemoteCall、iListenAttentively 或 PLand。

| 模块 | 业务职责 |
|---|---|
| EternalCore | 唯一身份、权限、资产、事务、账本、审计、回执与恢复权威 |
| EternalCommerce | 钱庄、税、交易、商店与寄售 |
| EternalLife | 签到、成长、任务进度与资格 |
| EternalWorld | 传送、保护道具、死亡、PVP、NPC 与宅地 |
| EternalContent | 公告、委托与活动定义 |
| EternalManagement | 职司、管理面板、审批与处罚 |
| EternalPresentation | UI、主题、HUD、MOTD、诗笺与粒子 |
| EternalEncounters | Boss、战斗归因与奖励请求 |

Core 当前是基础设施原型，资产 API 未开放；其余七项均为 **PLANNED / NOT IMPLEMENTED**，默认关闭。

## SDK 与开发

跨 DLL 使用 Stable C ABI，开发层提供 Modern C++ EternalSDK。API 1.0 与模块 ABI 1.0 是独立版本草案；结构携带版本与大小，能力须显式查询。边界不传递 STL 对象、异常或 SQLite 句柄，分配方负责释放。同步通信走 Service Registry，广播走 EventBus；禁止读取其他模块私有源码或数据库。

新模块从 [标准模板](templates/EternalModule/README.md) 开始，只需阅读 [SDK](sdk/EternalSDK/README.md)、[MODULE_DEVELOPMENT](MODULE_DEVELOPMENT.md) 和 Example Module。优先扩展已有业务域，形成独立业务域才增加 DLL target。

## 安装与配置

1. 单独准备合法取得的 BDS **1.26.51.1** 与 LeviLamina **26.51.6**；本仓库不分发它们。
2. 从通过的 [Windows Actions 构建](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/workflows/build.yml) 下载 `EternalWorld-windows-x64` Artifact，校验 ZIP 对应的 `.sha256`，将 `Eternal/` 放入服务端 `plugins/`。
3. 将 `config/modules.example.json` 复制为 `config/modules.json`。Core 必须启用，七个业务骨架保持关闭。配置、私有数据、日志和资源分别存放在对应目录。
4. 启动后检查 `ecore status`、`ecore selfcheck`、`eternal status`、`ll list`；后者应只列出 Eternal。替换 DLL 前正常 `stop`，更新保留配置与数据。

[部署工具](tools/Deploy-Host.ps1) 可复制本地构建，并仅在缺失时创建活动配置；[配置示例](config/modules.example.json) 不含运营者身份。

## 编译、Artifact 与 Release

主要环境是 GitHub Actions Windows x64 / Server / Release，固定 LLVM **22.1.0**、XMake **3.1.1**、C++20 与 **MD** runtime，来源与摘要见 [构建锁](tools/build-lock.json)。Host 和每个模块拥有独立 target；CI 构建九个 DLL，运行 SDK、Core、Host、配置和实际 DLL 测试，再生成 `EternalWorld-<commit>-windows-x64.zip` 与 SHA256。

本地复现与保留的便携工具见 [BUILD](BUILD.md)。云 CI 不运行 BDS；实服验收独立记录。Artifact 只含 Eternal、自研 DLL、示例配置、构建信息和许可证，不含服务端、世界、数据库或日志。

[Release 工作流](.github/workflows/release.yml) 对已有 `v0.x.y-alpha.N` tag 重新 Build / Test / Pack / SHA256，然后建立草稿预发布。版本记录 Eternal、API、ABI、LL 与 BDS 兼容组合。当前未宣称稳定 1.0 或已经发布 Release。

贡献请阅读 [CONTRIBUTING](CONTRIBUTING.md) 并附复现与测试；安全报告遵循 [SECURITY](SECURITY.md)，不要公开玩家资料或凭据。Phase 1 回滚标签只保留在本地私人历史中。
