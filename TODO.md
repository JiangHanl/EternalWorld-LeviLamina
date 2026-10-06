# 下一步

Phase 2 已标记 DEVELOPMENT COMPLETE，生产保持 PENDING REAL CLIENT。七业务模块仍 PLANNED / disabled，正式资产 feature bits 为 0。详见 [策略](TESTING.md)、[Phase 2 计划](docs/PHASE2_PLAN.md)、[报告](docs/PHASE2_TEST_REPORT.md) 和 [云证据](docs/phase2-sdk13-cloud-evidence.json)。

1. 每次更新都执行文档、工作树/索引、公开父链与Artifact摘要审查；身份/路径/凭据/许可证检查是长期门槛，私人历史与基线标签不推送。
2. SDK 1.3、八身份 harness、真实 DLL 消费者/崩溃重放与生产隔离已完成完整构建及测试，保留 1.0 / 1.1 / 1.2 前缀回归。
3. 新源码公开审计、GitHub Actions、两个 Artifact 下载与摘要核验、云正式 DLL 独立 BDS 两轮启停/诊断/disable-enable/terminal cleanup 已完成。
4. Phase 2 已标记 DEVELOPMENT COMPLETE，生产 PENDING REAL CLIENT，可进入 Phase 3。剩余真实客户端项目 DEFERRED_REAL_CLIENT，统一放在 [开服前清单](PRE_RELEASE_CHECKLIST.md)。
5. 保持固定BDS/LL/工具链组合，更新时重跑CI、准确产物摘要与真实BDS启停验收；停服必须确认terminal cleanup成功，不能只看进程退出码。

Phase 3（Commerce）：先定义测试再实现，见 [计划](docs/PHASE3_PLAN.md)。

1. 编号 001 Commerce migration：转账/免税额度、伴礼、收购、寄售、交付与回执引用；验证 fresh/upgrade/失败回滚/未知新版拒绝/备份。
2. 纯业务域测试：税费分档与每日额度、伴礼手续费、寄售竞态与 7 天退回、收购配额与衰减。
3. 真实模块 ABI：去掉 PLANNED、声明 Core 依赖与能力、实现五导出与 Enable/Disable 资源回收。
4. 转账税最小闭环：Commerce 计算 + Core 授权写入 + 幂等回执引用，重放不重复扣款。
5. 伴礼/寄售/收购接入与并发购买、交付 Pending/Reconciliation 消费者；公开审计、云 CI、Artifact 核验与真实 BDS 回归。

具体缺陷不得用文档上的最终设计掩盖：独立域通过不代表原生交易上线；Host实例上下文不代表玩家资产授权；.gitignore不能清除已提交的私人历史。
