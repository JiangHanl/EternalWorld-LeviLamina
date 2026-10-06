# Phase 3：Commerce 实施与验收

## 基线与范围

绿色源码为 `aef8f1214b85e1ac3ebe4ecc0f2ce205a8393618`（SDK 1.3，Phase 2 DEVELOPMENT COMPLETE），云 CI 为 [37485499524](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/runs/37485499524)。Phase 2 的历史证据不改写；本阶段另写报告与证据。

只实现 `EternalCommerce` 模块的钱庄、转账税、伴礼、收购、寄售与物品交付。其余六个业务模块继续 PLANNED / disabled；正式资产 feature bits 仍为 0。Commerce 只能通过 `EternalCore.Phase2Api` 1.3 专属绑定与公开 EventBus 访问 Core，禁止查询 `core.native.*`，禁止自建权威余额、钱包或权限副本。

## 业务规则

以下为可公开摘要；最终数值与交互取 v3.3.3 产品规格及后续会话修订，开始实现每个子项前先复核完整规格，不把旧 JS 行为当作权威。

| 子项 | 规则摘要 | 落地归属 |
|---|---|---|
| 转账税 | 每日个人累计免税额度 1000 两；超额部分拆单，最低 5 两；税率 2/2/3/5% 内扣（具体档位边界以完整规格为准） | Commerce 计算税费与每日额度，Core 执行最终转账 |
| 钱庄 | 碎银/银两展示、余额、存取/兑换入口；不做第二权威余额 | Commerce 页面与流程，Core 账户为唯一余额 |
| 伴礼 | 3—20 人、10 两起/份、最高 3000 两、2% 手续费、300 秒有效 | Commerce 持久化礼包与参与名单，Core 按名单执行扣款/入账 |
| 收购 | 收购配额/衰减与合成套利闭环审查，防刷与定价边界 | Commerce 持久化收购单与配额，Core 执行结算 |
| 寄售 | 并发购买、7 天退回、商品中文显示与 NBT 完整；同件并发只能一人成交 | Commerce 持久化寄售单与竞态控制，Core 执行钱款结算，物品交付走 Pending/Reconciliation |
| 交付 | BDS 背包不在 SQLite 事务内，不承诺 exactly-once；不确定时停止自动重发并保留人工核验 | Commerce 持有交付申请与 Pending/Reconciliation 状态，物品发放经 Host 引擎入口后回写结果 |

转账税是 Commerce 与 Core 协作的最小闭环，先实现它来验证“业务规则计算 + 授权 Core 写入 + 幂等回执”整条链，再扩展伴礼、寄售与收购。

## 数据所有权与模块边界

Commerce 只打开自己的私有数据库，表规划为：转账/免税额度记录、伴礼包与参与名单、收购单与配额、寄售单与竞态锁、库存申请与交付状态、正式资产回执引用。Core 仍是 coin 与声望的唯一写入者；Commerce 先持久化业务申请与幂等键，再通过经授权的 Core 接口申请结果，结果未知时用同一键查询/重放，不改键重复扣款。

依赖声明必须真实：Commerce 声明对 Core 的 required 依赖，并在首次发现时声明本次 Enable 要用的完整非零能力（Events、Audit 及资产/权限读写所需能力）。服务发现、Invocation 路由与后台消费者都按 [MODULE_DEVELOPMENT.md](../MODULE_DEVELOPMENT.md) 与 [Phase 2 契约](PHASE2_API.md) 执行，不复制测试专属 issuer，不自报 actor/module。

## 实施顺序（测试先行）

1. 编号 `001` 的 Commerce migration：转账/免税额度、伴礼、收购、寄售、交付与回执引用；验证 fresh / upgrade / 失败回滚 / 未知新版拒绝 / 备份，与 Core 迁移同样的单写入口和校验。
2. 纯业务域：税费分档与每日额度、伴礼手续费与名单、寄售竞态与 7 天退回、收购配额与衰减，全部用临时 SQLite 做 DOMAIN 测试，不依赖 BDS。
3. 模块 ABI：真实描述符（去掉 PLANNED 标志、声明 Core 依赖与能力）与五个导出；Load 只验证私有配置，Enable 发布服务，Disable 撤销路由/消费者，Unload 释放资源。
4. 最小闭环：转账税——Commerce 计算税费与额度，经 Core Invocation/能力链提交扣款+入账，保存回执引用，幂等重放不重复扣款。
5. 伴礼、寄售、收购逐步接入；寄售的并发购买与物品交付分别用 SYNTHETIC_INTEGRATION 与 REAL_DLL 验证。
6. 后台消费者：交付申请经 registerConsumer/queryConsumer/acknowledgeConsumer/retryConsumer 处理 Pending/Reconciliation；提交副作用与 eventId 去重原子化后再 ACK，进程中断后重放同 eventId。
7. 公开审计、稳定提交、GitHub Actions、两个 Artifact 下载核验、云正式 DLL 独立 BDS 回归。人工客户端不作为日常开发阻塞项。

## 事务与失败语义

所有金额增减经 Core 事务，保存 transactionId、幂等 key、payload hash、钱包、账本、审计、回执与 outbox 原子提交。Commerce 先落自己的业务申请和幂等键，再请求 Core；同 key 同请求返回原回执，同 key 异请求拒绝，不能先扣钱再创建可能重复的业务单。跨 Commerce 库、Core 库与 BDS 背包不是同一事务；物品交付只定义 Pending/Reconciliation，不确定时不自动重发。

寄售同件并发购买只能一人成交：在 Commerce 库内用单写事务 + 版本/条件更新决定成交者，失败方收到明确 CONFLICT，不出现负库存或双发。7 天退回按持久截止时间结算，重启后仍可查询。

## 验收分层

沿用 UNIT、DOMAIN、SYNTHETIC_INTEGRATION、REAL_DLL、REAL_BDS、CLOUD_CI、REAL_CLIENT 七类（见 [TESTING.md](../TESTING.md)）。前六类全部通过、云 Artifact 真实 BDS 回归通过后，Phase 3 才可标记 DEVELOPMENT COMPLETE；生产保持 PENDING REAL CLIENT，真实客户端项统一 DEFERRED_REAL_CLIENT，集中在 [开服前清单](../PRE_RELEASE_CHECKLIST.md)。

新增测试必须在 [test-suites.json](../tools/test-suites.json) 和 xmake/便携构建里登记分类，生产 ZIP 不得包含测试入口或验证变体。合成身份、临时数据库与真实 DLL 证明内部行为；只有真实认证 Player 才是线上身份来源。

## 回滚

Commerce 未启用或停用时不得写 Core 资产。停服后使用已验收的 Phase 2 ZIP 恢复 Host 与八模块；Phase 3 数据库保留原文件及编号迁移前备份，Phase 2 不认识新版 Commerce schema，不能用旧模块打开新版数据库。物品交付停止自动重发时保留人工核验入口。
