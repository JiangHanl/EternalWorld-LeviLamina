# 下一步

Phase 1.5 的历史验收保留，当前只推进 Phase 2。七个业务模块仍为 PLANNED / disabled，正式资产 feature bits 为 0。详见 [Phase 2 计划](docs/PHASE2_PLAN.md) 和 [报告](docs/PHASE2_TEST_REPORT.md)。

1. 每次更新都执行文档、工作树/索引、公开父链与Artifact摘要审查；身份/路径/凭据/许可证检查是长期门槛，私人历史与基线标签不推送。
2. 已完成 Runtime/Host 回归、完整九 DLL 构建和 SDK 1.2 契约；新增源码变动时继续复核缓存、会话代次、过期/撤权、拒绝回执及 DTO。
3. 新版公开源码的 GitHub Actions、两个 Artifact 下载校验和云 DLL 两轮真实 BDS 生命周期回归已通过，证据绑定源码 8cf831f。后续报告更新仍审查独立公开父链。
4. 服主认证、Money/Reputation、幂等、拒绝回执、公开 SDK 和过期页面已验证；服务端重启保持已内部核对。按用户要求暂停新增客户端步骤，剩余真实重连、普通玩家授予/撤权、旧 UI 和双人转账保留 NOT RUN。当前停在本阶段报告审核处，不进入 Phase 3。
5. 保持固定BDS/LL/工具链组合，更新时重跑CI、准确产物摘要与真实BDS启停验收；停服必须确认terminal cleanup成功，不能只看进程退出码。

具体缺陷不得用文档上的最终设计掩盖：独立域通过不代表原生交易上线；Host实例上下文不代表玩家资产授权；.gitignore不能清除已提交的私人历史。
