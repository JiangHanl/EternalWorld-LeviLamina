# 下一步

Phase 1.5 的历史验收保留，当前只推进 Phase 2。七个业务模块仍为 PLANNED / disabled，正式资产 feature bits 为 0。详见 [Phase 2 计划](docs/PHASE2_PLAN.md) 和 [报告](docs/PHASE2_TEST_REPORT.md)。

1. 每次更新都执行文档、工作树/索引、公开父链与Artifact摘要审查；身份/路径/凭据/许可证检查是长期门槛，私人历史与基线标签不推送。
2. 完成当前 Runtime/Host 引擎接入回归和完整九 DLL 构建，复核缓存、会话代次、过期/撤权、拒绝回执及 SDK DTO。
3. 提交经审查的独立公开父链，完成本阶段 GitHub Actions、Artifact 下载与云 DLL 的真实 BDS 生命周期回归。
4. 配合真实服主客户端验证身份、权限、Money/Reputation、幂等、余额不足和重连/重启恢复。普通玩家授予/撤权及旧 UI 测试需第二账号；缺少时保留 NOT RUN。完成本阶段报告后停止，等待审核再讨论 Phase 3。
5. 保持固定BDS/LL/工具链组合，更新时重跑CI、准确产物摘要与真实BDS启停验收；停服必须确认terminal cleanup成功，不能只看进程退出码。

具体缺陷不得用文档上的最终设计掩盖：独立域通过不代表原生交易上线；Host实例上下文不代表玩家资产授权；.gitignore不能清除已提交的私人历史。
