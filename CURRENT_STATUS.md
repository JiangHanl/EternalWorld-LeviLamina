# 当前状态

更新：2026-10-04。当前正在Phase1.5工程化；最终运行架构为一个EternalHost LL插件、八个内部DLL和EternalSDK C/C++接口。架构文档描述目标，不是已完成业务清单。

| 范围 | 已知结果 |
|---|---|
| Phase1最小Core | 历史真实BDS加载/健康命令/ABI自检/停用恢复/正常停服/完整重启通过 |
| 独立事务域 | 17组真实SQLite/C++测试通过，含进程退出恢复；与旧基线实现保持一致 |
| Phase1.5本地构建与测试 | Host与八内部DLL完成本地编译/链接，七个console测试及Core/Module两套SDK契约验证通过；不等同官方xmake云CI或BDS验收 |
| Host线程交接 | 首次及终止交接通过23组mock与新Host真实复验；最终交接须OS句柄证明服务器线程退出 |
| Phase1.5真实BDS | PASS：新Host两次启动、诊断、手动停用恢复、完整重启、明确终止清理成功及退出码0；无玩家 |
| CI与发布包 | 首次云CI在libhat官方依赖安装/下载阶段失败，尚未进入业务源码编译；下载修复源码已准备，尚未推送重跑，云CI未通过 |
| 源码公开审查 | 首次main源码提交已公开，156文件暂存审查与独立公开父链核对通过；后续修改继续复核 |
| 原生玩家资产/能力链 | 未验收，外部资产API关闭 |
| 业务功能/旧数据迁移/客户端视觉 | 未实现或未验收 |

历史Phase1固定组合为BDS1.26.51.1/LL26.51.6，最小DLL摘要与验收在docs/phase1-evidence.json。旧架构曾因遗漏SymbolProvider在首命令失败，补官方依赖后重启回归通过。这个结论不能移用到新的Host DLL。

Phase1.5最初的manifest误将加载器声明为插件依赖，已移除并采用server平台声明；随后真实启动暴露Load发生在启动线程、Enable发生在服务器线程。Host仅在成功Load、首次Enable尝试之前且完全静止时显式交接一次绑定线程，之后拒绝旧线程和再次交接。

此前Host在BDS1.26.51.1/LL26.51.6的启动与命令检查通过，但rawlog复查发现stop换线程导致Host停用失败。先前仅凭退出码0得出的完整生命周期PASS已撤回，该失败记录保留。

终止交接现已通过23组mock及新Host真实复验：保留服务器线程Windows同步句柄，Stopping期间的非绑定线程请求只延后；typed leaveGameSync的origin返回后，必须确认该句柄已signaled才最终交接、逆序停用/卸载并永久禁止启用。普通Disable线程限制不放宽，SDK仅增加契约注释、布局不变。

新Host的两次真实运行均通过启动、Core服务健康检查、手动停用/恢复、自检与完整重启检查；两次stop都明确记录内部模块已停用并卸载的cleanup PASS，退出码均为0，日志无关闭Eternal错误。LL列表仅一个Eternal mod，features=0，未连接玩家，服务器已停止。Host SHA-256为4a4bef31d753fb07918b4d0fee4e7a4ea6475cb3e14d012e0844e4e6ccade9db；对应证据在docs/phase1.5-evidence.json。阶段仍为1.5，云CI尚未通过，不进入Phase2。

Core域只成功请求有正式持久回执；拒绝请求、队列工作流、邮件/物品交付、原生投影及外部能力链仍待实现。测试身份为合成数据，运营者身份由私人配置提供，不在源码预置。

阅读顺序：ARCHITECTURE → API/ABI → DATABASE → BUILD/DEPENDENCY → TEST_REPORT/TODO。公开树审计和许可证检查是发布门槛，不等同玩家安全或功能验收。
