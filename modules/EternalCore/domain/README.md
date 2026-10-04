这是第二阶段的纯 C++20/SQLite 事务域原型，独立于 LeviLamina/LSE。它已经通过便携编译器的域测试，但尚未接入真实原生玩家身份认证、调用者 scope/capability 链、UI 或资产投影。外部资产 API 仍应返回 unsupported；不能把域测试通过表述为原生插件交易功能已上线。

`Core.hpp` 是内部实际接口。`Core(dbPath, ownerXuid)` 的服主 XUID 必须由 native glue 的可信本地配置传入，不在源码中预置运营者身份。测试只用合成身份。所有资产使用 `int64_t`，coin 的单位是分，100 分为一两；reputation 是整数声望点，没有浮点金额。

| 接口 | 用途与限制 |
|---|---|
| `ensurePlayer(VerifiedIdentity)` | 只供服务端可信加入/命令 origin 适配注册 UUID/XUID；不作为玩家 create 命令公开 |
| `balance(uuid, Asset)` | 查询唯一 SQLite 权威资产，离线账户也存在 |
| `hasRole(Actor, Role)` | 服主隐式拥有全部职司，其他玩家只具有持久化授权 |
| `setRole(owner, target, role, enabled, reason, key)` | 服主授权/撤销，普通 OP 或 operations 职司不能授权 |
| `ownerGrant(owner, target, asset, amount, reason, key)` | 仅服主可发行 coin 或 reputation |
| `transfer(caller, target, amount, reason, key)` | 转移调用者自己的 coin，可向已注册离线账户转账；声望不可转账 |
| `pendingOutbox(limit)` / `acknowledgeOutbox(id)` | 服务端可信投影 worker 获取、确认持久事件；不修改权威余额 |
| `backup(newPath)` | 对当前 Native 数据库使用 SQLite online backup，目标必须不存在 |

`Actor` 没有公开构造函数，由可信注册返回。`VerifiedIdentity` 仍是服务端边界输入，并不自行认证一个声称的 XUID；native glue 必须从服务端认证后的实体/origin 获取它，不能从聊天参数、网络 JSON、source/provider 名称拼装。XUID 为规范非零十进制字符串，解析范围不得超过 UINT64_MAX；原因与请求键必须是合法 UTF-8，拒绝过长编码、代理码点、超出 Unicode 范围、孤立续字节、非法字节与 ASCII 控制码。当前原型拒绝普通玩家 grant/setRole，但没有声称完成跨插件能力凭证认证。

每个成功操作有独立 transactionId；幂等唯一作用域为固定 executorScope `core.domain.v1`、可信 Actor UUID 与 idempotencyKey 的组合，不同玩家可各自使用相同 key。Canonical request v1 按固定字段顺序和字节长度编码，包含操作、固定执行域、已绑定调用者 UUID/XUID、目标、资产/职司、金额/开关和原因。身份校验后，只在该 Actor 的固定执行域中查找回执。同一玩家、同一 key 与同一请求返回原样持久化 JSON 回执；同一玩家改金额、原因或操作后复用 key 返回 conflict，不重复记账。receipts 以 transactionId 归属交易，不另保存一份可不一致的请求键作用域。

交易在 `BEGIN IMMEDIATE` 内原子提交 accounts、transactions、entries、receipts、audit、outbox 和 pending_receipt。SQLite 运行在 WAL 与 synchronous FULL。transactions 保存 transactionId、executorScope、actorUuid、idempotencyKey、status、createdAt、updatedAt、payload、requestHash 和 retryCount。当前只成功交易有 durable committed 记录；busy、invalid、overflow、权限拒绝和冲突返回类型化状态，没有另外生成正式拒绝回执，也没有实现 queued/failed 业务工作流。

pending_receipt 的 `pending_projection` 表示资产已经提交，UI/记分板投影尚未全部确认。它不是未完成的资产事务。最后一个 outbox 事件确认后删去 pending_receipt，正式 receipts 永久保留，重放不重新创建已确认事件。未来 Worker 必须在执行投影时重新读取当前 Core 权威余额/职司，再设置并确认；历史事件 payload 仅作变更通知，不能覆盖较新的余额。单个目标的投影应串行执行；若改成并发投影，则须维护资产 eventId 的单调版本并拒绝旧投影，避免早先读取的值晚于新值写入。当前没有实现投影 Worker。

唯一编号 SQL 位于 `migrations/EternalCore/001_initial.sql`，`Schema.hpp` 包含运行时嵌入版本。测试按内容逐字核对二者，schema_migration 保存版本、SQL SHA-256 与应用时间。启动拒绝更高版本、未知现存表结构、不同持久化服主或 migration checksum 不符的数据库。未来迁移必须按编号增加，不能编辑已经应用的 001。

首次创建不存在的 Native 数据库时，直接在事务中建立 schema，不备份空库；已存在且没有业务表的 Native 空数据库，首次迁移前才生成 `dbPath.pre-migration-001-时间.sqlite` 在线备份。未来已有 schema 的编号升级同样必须先在线备份。迁移事务内及启动完成前执行 integrity_check、foreign_key_check。在线备份目标同样验证后，记录 `.sha256` 旁文件。失败可能留下候选文件；只有旁文件内容完整、重新计算 SHA-256 匹配并重新验证数据库完整性与外键的备份才可恢复，不能只凭文件存在。失败后同一个目标路径会被拒绝，必须使用新路径，避免覆盖不明确的候选快照。此代码和测试没有打开旧 BDS 数据库或导入旧脚本数据。

验证命令：

```powershell
& '.\tools\Build-DomainTests.ps1'
```

该脚本使用官方 SQLite amalgamation 与 LLVM-MinGW 构建独立域测试；它不能用于生成 LeviLamina DLL。本次另以 `-Wall -Wextra -Werror` 严格编译成功，17 组测试通过，覆盖整数资产、身份唯一、XUID/UTF-8 边界、权限拒绝、玩家作用域幂等冲突、并发连接、busy、溢出、outbox、首次建库无备份、已有空库迁移备份、备份校验以及恢复。

两类故障验证分别为 C++ 异常注入和真实子进程 `_Exit(86)`：扣款后、提交前、提交后立即终止，随后重开同一测试数据库并重放同 key。前两处不保留资产变动，提交后保存原回执，重放只发生一次资产转移。`_Exit` 模拟进程丢失，不等同于断电、存储硬件故障或 BDS 原生交易验收。
