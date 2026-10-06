# 路线图

| 阶段 | 门槛 | 状态 |
|---|---|---|
| Phase 1 | 最小原生Core/SDK诊断在真实BDS加载、生命周期和重启 | 已验证历史基线 |
| Phase 1.5 | Host LL-only、八内部模块握手、SDK、构建/CI/Artifact及实服启停、公开安全与许可证 | 工程化验收完成；最终文档提交按发布流程复核 |
| Phase 2 | 可信身份、配置、权限/能力链、Core资产与正式回执、Outbox和原生恢复 | DEVELOPMENT COMPLETE；生产 PENDING REAL CLIENT |
| Phase 3 | Commerce钱庄/税/伴礼/收购/寄售与交付 | 规划 |
| Phase 4 | Life首入/签到/补签/成长/进度/知己/资格 | 规划 |
| Phase 5 | World传送/保护道具/死亡/NPC/PVP/维护/原生宅地 | 规划 |
| Phase 6 | Content规则/公告/委托/活动定义与发布 | 规划 |
| Phase 7 | Management职司/管理时钟/审批/处罚/业务管理流程 | 规划 |
| Phase 8 | Presentation主题/HUD/MOTD/诗笺/提示/粒子 | 规划 |
| Phase 9 | Encounters Boss/战斗归因/资格/幂等奖励 | 规划 |
| Phase 10 | 迁移对账、全系统恢复/安全/负载、真实客户端验收 | 规划 |

每阶段先定义测试，再实现、构建、验证并更新证据。不得用空壳DLL、目录数量、feature标记或旧架构测试代替当前阶段结果。后续业务不能绕过Core授权/事务门槛。

所有阶段采用 [七类测试策略](TESTING.md)。内部验证与云 Artifact 回归全部通过可继续下一阶段；真实客户端缺项保持 DEFERRED_REAL_CLIENT，生产为 PENDING REAL CLIENT。公开 Beta / 正式开服前统一执行 [人工门槛](PRE_RELEASE_CHECKLIST.md)。

Phase1.5已分别验收本地产物与实际Windows云Artifact的两次BDS运行和终止清理。七个业务模块仍为PLANNED且默认禁用，Core资产API保持UNSUPPORTED；完成基础加载和工程化不表示后续业务阶段已实现。
