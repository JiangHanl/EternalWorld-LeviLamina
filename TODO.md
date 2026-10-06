# 下一步

Phase 1.5 的历史验收保留，当前执行 Phase 2 自动化收尾。七个业务模块仍为 PLANNED / disabled，正式资产 feature bits 为 0。详见 [策略](TESTING.md)、[Phase 2 计划](docs/PHASE2_PLAN.md) 和 [报告](docs/PHASE2_TEST_REPORT.md)。

1. 每次更新都执行文档、工作树/索引、公开父链与Artifact摘要审查；身份/路径/凭据/许可证检查是长期门槛，私人历史与基线标签不推送。
2. 完成 SDK 1.3、八身份 harness、真实 DLL 消费者 / 崩溃重放与生产隔离的完整构建及测试，保留 1.0 / 1.1 / 1.2 前缀回归。
3. 新源码公开审计、GitHub Actions、两个 Artifact 下载与摘要核验，使用云正式 DLL 自动完成独立 BDS 两轮启停、诊断、disable/enable、数据库检查及 terminal cleanup。
4. 所有内部门槛通过后标记 Phase 2 DEVELOPMENT COMPLETE，生产 PENDING REAL CLIENT，并允许进入 Phase 3。没有日常人工客户端任务；剩余真实项目为 DEFERRED_REAL_CLIENT，统一放在 [开服前清单](PRE_RELEASE_CHECKLIST.md)。
5. 保持固定BDS/LL/工具链组合，更新时重跑CI、准确产物摘要与真实BDS启停验收；停服必须确认terminal cleanup成功，不能只看进程退出码。

具体缺陷不得用文档上的最终设计掩盖：独立域通过不代表原生交易上线；Host实例上下文不代表玩家资产授权；.gitignore不能清除已提交的私人历史。
