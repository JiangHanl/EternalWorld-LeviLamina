# 当前状态

更新：2026-10-04。Phase 2 实现与验收进行中，范围为 Core 认证身份、权限、Capability、Money/Reputation、事务、审计、回执与 Outbox。详细当前结果以 [Phase 2 独立报告](docs/PHASE2_TEST_REPORT.md) 为准。正式资产能力保持关闭，七个业务模块保持 PLANNED / disabled；不会自动进入 Phase 3。

下表及其后段落是保留的 Phase 1.5 历史范围；本阶段的新源码、DLL 和真实玩家结果单独验证，不能继承其通过结论。

| 范围 | 已知结果 |
|---|---|
| Phase1最小Core | 历史真实BDS加载/健康命令/ABI自检/停用恢复/正常停服/完整重启通过 |
| 独立事务域 | 17组真实SQLite/C++测试通过，含进程退出恢复；与旧基线实现保持一致 |
| Phase1.5本地构建与测试 | Host与八内部DLL完成本地编译/链接，七个console测试及Core/Module两套SDK契约验证通过；不等同官方xmake云CI或BDS验收 |
| Host线程交接 | 首次及终止交接通过23组mock与新Host真实复验；最终交接须OS句柄证明服务器线程退出 |
| Phase1.5真实BDS | PASS：本地产物与实际云Artifact分别完成两次启动/停服、手动停用恢复、自检和完整重启；终止cleanup PASS、退出码0，无玩家 |
| Windows云CI与Artifact | PASS：源码93dbe05eba4410b73c5c9ce27e16888949e49d58，run37199429155/job111427872268；Artifact11302132760，39条allowlist、九个DLL |
| 源码公开审查 | 首次156文件源码索引与公开父链核对通过；当前163文件工作树与收尾索引扫描通过，每次更新持续复核公开父链 |
| 原生玩家资产/能力链 | Phase2未实现，Core资产API返回UNSUPPORTED，feature bits为0 |
| 七个业务模块/迁移/客户端视觉 | 七模块PLANNED且默认禁用；功能、旧数据迁移和玩家客户端验收未完成 |

历史Phase1固定组合为BDS1.26.51.1/LL26.51.6，最小DLL摘要与验收在docs/phase1-evidence.json。旧架构曾因遗漏SymbolProvider在首命令失败，补官方依赖后重启回归通过。这个结论不能移用到新的Host DLL。

Phase1.5最初的manifest误将加载器声明为插件依赖，已移除并采用server平台声明；随后真实启动暴露Load发生在启动线程、Enable发生在服务器线程。Host仅在成功Load、首次Enable尝试之前且完全静止时显式交接一次绑定线程，之后拒绝旧线程和再次交接。

此前Host在BDS1.26.51.1/LL26.51.6的启动与命令检查通过，但rawlog复查发现stop换线程导致Host停用失败。先前仅凭退出码0得出的完整生命周期PASS已撤回，该失败记录保留。

终止交接现已通过23组mock及新Host真实复验：保留服务器线程Windows同步句柄，Stopping期间的非绑定线程请求只延后；typed leaveGameSync的origin返回后，必须确认该句柄已signaled才最终交接、逆序停用/卸载并永久禁止启用。普通Disable线程限制不放宽，SDK仅增加契约注释、布局不变。

本地Host的两次真实运行均通过启动、Core服务健康检查、手动停用/恢复、自检与完整重启检查；两次stop都明确记录内部模块已停用并卸载的cleanup PASS，退出码均为0，日志无关闭Eternal错误。LL列表仅一个Eternal mod，features=0，未连接玩家。该Host SHA-256为4a4bef31d753fb07918b4d0fee4e7a4ea6475cb3e14d012e0844e4e6ccade9db；对应证据在docs/phase1.5-evidence.json。

实际Windows云CI已通过，其下载Artifact随后部署至BDS1.26.51.1/LL26.51.6并完成两次真实运行：首轮手动disable/enable、自检、完整重启、双终止cleanup PASS及退出码0；无玩家，服务器已停止。云Host SHA-256为c0e69f8818e997e4a4eb0c2de9bfc911ac1debc3f039c6839e18b05f4614d994，使用固定LLVM22.1.0、XMake3.1.1、C++20、MD及API/ABI1.0。独立云证据在docs/phase1.5-cloud-evidence.json。本地与云产物分别验收，不能混用摘要。

首次云CI37193348084在libhat官方依赖安装/下载阶段失败，后续改用固定摘要的官方归档后通过，失败历史保留。Phase1.5完成不表示玩法交付，Phase2可信身份、权限/能力链与玩家资产接入尚未实现。

云构建与产物验收严格绑定源码93dbe05eba4410b73c5c9ce27e16888949e49d58。本轮后续仅文档/证据更新，不表示最终文档提交重新编译了DLL；产物仍按各自摘要识别。

Core域只成功请求有正式持久回执；拒绝请求、队列工作流、邮件/物品交付、原生投影及外部能力链仍待实现。测试身份为合成数据，运营者身份由私人配置提供，不在源码预置。

阅读顺序：ARCHITECTURE → API/ABI → DATABASE → BUILD/DEPENDENCY → TEST_REPORT/TODO。公开树审计和许可证检查是发布门槛，不等同玩家安全或功能验收。
