# 数据所有权与数据库

每个模块只打开自己的私有数据库；Core 是身份、正式资产、权限和回执的唯一权威。SQLite 使用 WAL、synchronous FULL、foreign_keys=ON，写入通过事务单写入口；busy 不当作成功。

## 当前 Core 域原型

| 表 | 已实现约束 |
|---|---|
| players | UUID 与 XUID 分别唯一；XUID 为非零规范 uint64 十进制，名字不是授权键 |
| accounts | coin 分/声望点用 int64，非负；Core 唯一写入 |
| roles | 可信 UUID 归属；服主来自私有配置，普通职司不能发行资产 |
| transactions | transactionId、executorScope、actorUuid、key、hash、payload、状态、时间、retryCount |
| entries | 资产变动与变更后余额，引用正式交易 |
| receipts | transactionId 主键，持久精确回执 |
| audit | 主体、操作、原因、hash、时间与 transactionId |
| outbox/pending_receipt | 已提交资产的投影事件与待确认回执，不是半笔资产事务 |
| schema_migration | 有序版本、SQL checksum、应用时间 |

旧内部执行域保留 core.domain.v1，Phase 2 注册模块使用 Core 从真实 moduleId 派生的作用域；UNIQUE(executorScope, actorUuid, idempotencyKey)。经过身份校验后只查询该主体和模块作用域回执。金额/原因/操作等 canonical payload 不同则拒绝复用；合法 UTF-8 与整数溢出在提交前校验。

Phase 2 编号 002 迁移增加稳定 PlayerId、显示名、时间、身份/权限版本、账户 revision、扩展 Role、committed/rejected 事务、请求来源审计、有限拒绝审计聚合及每消费者 Outbox 状态。旧 001 校验保留。高价值拒绝形成 rejected 回执，钱包/账本/成功事件保持不变；同 key 冲突不覆盖原回执。规范化请求的回执保存完成时间与当次账户/权限快照，后续余额变化或重启不能改变历史结果。邮件、物品 exactly-once、业务排队和实物核验仍未实现。

## 其他模块私有数据规划

| 所有者 | 私有数据 |
|---|---|
| Commerce | 市场订单、寄售、库存申请与正式资产回执引用 |
| Life | 签到/补签、成长、玩家进度、朋友、资格与领奖引用 |
| World | 居所、地标、死亡记录、NPC、交互道具、土地几何/成员/保护标志 |
| Content | 公告、规则、任务定义、内容版本与发布时间 |
| Management | 审批/处罚流程和各业务结果引用，不复制钱包或权限 |
| Presentation | 主题与显示设置，不拥有正式资格 |
| Encounters | 模板、实例、战斗贡献、资格与奖励申请引用 |

World 土地坐标须包含世界实例 ID、维度和边界；购买/扩建在事务内检查相交及空间索引。不能先扣钱再创建一个可能重叠的区域。

## 持久性与恢复

首次创建新 Native 数据库直接事务建 schema，不备份空文件。已有空 Native 数据库初始化或未来 schema 升级前，使用 SQLite online backup API，验证 integrity_check、foreign_key_check 与 SHA-256，再事务执行编号 SQL 并记录 checksum。未知较新 schema、未知业务表、持久服主/hash 不一致均拒绝启动。

失败备份可能留下候选文件；旁文件内容完整、SHA匹配和数据库检查通过后才可恢复。文件存在不是成功证据；失败同目标路径拒绝覆写是安全阻断。禁止复制活跃 WAL 主库冒充一致快照。

outbox 消费时读取当前 Core 权威值并按目标串行，或用资产 eventId 单调版本拒绝旧投影，禁止历史 payload 覆盖新余额。跨模块库与 BDS 背包不能由 SQLite 保证原子提交；物品交付不确定时停止自动重发，保留人工核验。
