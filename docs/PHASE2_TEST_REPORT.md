# Phase 2 验收报告

状态：本轮实现、内部回归和可执行的服主验收已完成；Phase 2 整体验收未完成，剩余真实场景明确保留 NOT RUN。Phase 1.5 历史记录保持原样，本页只记录 Phase 2。更新于 2026-10-05。

公开汇总：[云产物与生命周期](phase2-cloud-evidence.json)、[服主与内部恢复验收](phase2-player-evidence.json)。只包含状态，不公开身份、余额、数据库或原始日志。

| 层次 | 当前证据 | 状态 |
|---|---|---|
| 原事务域 | 17 组历史语义回归 | PASS |
| Phase 2 私有域 | 30 组迁移、身份、权限、事务、拒绝回执、Outbox 历史/消费者和真实子进程中断恢复 | PASS |
| SDK 静态契约 | 旧布局保留、新 C11/C++20 DTO 布局及 19 组合成 C++ 客户端契约 | PASS |
| 引擎入口编译 | HostMod、Core Module、LLAdapter 三个 TU 使用固定真实 LL 头独立编译 | PASS |
| Host 模块绑定 | 新 17 组私有隔离、绑定代次、撤销、依赖能力、1.1 前缀兼容和回调保护；原 23 组继续通过 | PASS：合成 Host 测试 |
| Runtime | 42 组 Core 票据、公开 Invocation 路由、过期、旧 UI、预算、回执恢复、输入回归和实际线程退出证明 | PASS：合成 Runtime 测试 |
| 完整九 DLL 构建 | 正式九 DLL、独立验证 DLL、12 套测试及 4 套双语言布局验证 | PASS：本地固定工具链 |
| 实际 DLL 调用链 | 4 组真实 Host/Core/Fixture 服务发现、路由登记、默认 Unsupported、启停与卸载 | PASS：未认证玩家 |
| 本地 DLL 真实 BDS | 1.2 加临时 Fixture 的两轮启动、disable/enable、正常停服；初版 native 诊断新库为空、schema 2、外键/账本/回执一致 | PASS：尚无玩家连接 |
| GitHub Actions | SDK 1.2 源码 8cf831f，run 37217978119，12 套测试、152 组计数测试及双语言布局检查 | PASS |
| 云 Artifact 下载与校验 | 正式包 40 文件/9 DLL；独立验证包 2 文件；下载摘要、内包摘要、部署 DLL 摘要一致 | PASS |
| 云 Artifact 真实 BDS | 初始云 DLL 两轮启停；真实玩家测试后重启与停用恢复；撤下 Fixture 后正式目录两轮启停，均有 cleanup PASS / exit 0 | PASS：引擎生命周期 |
| 真实客户端：服主身份 | BDS Full authentication、trusted XUID / authenticated UUID 一致、私人 Owner 配置匹配、客户端 owner / money.adjust 允许 | PASS：单个真实账号 |
| 真实客户端：资产 | 新零资产测试库；Money 与 Reputation 增减、同 key 同请求重放、同 key 不同金额 CONFLICT、余额不足 | PASS：客户端操作及私有数据库对账 |
| 真实客户端：职司 | Owner 给自身授予 Builder，Role 行与授予 Audit 已持久化，重启保持，未新增原生 OP | PASS：实际撤销尚未执行 |
| 真实客户端：公开 SDK | 真实 Invocation、限定授权、公共资产服务、持久回执及 Outbox 查询；重复 key 只入账一次 | PASS：客户端与 Core/Fixture 交易证据 |
| 真实客户端：拒绝回执 | 余额不足有 rejected Receipt / Audit，没有 Ledger / Outbox；冲突保留原 Receipt 并审计 | PASS：过期见下一行，真实撤权未运行 |
| 真实客户端：Outbox | 每笔成功资产/职司事务产生事件；未确认消费者保持 retry，不伪造 ACK；SDK 可查询 | PASS：客户端重启后重读与真实消费者去重/ACK 未运行 |
| 真实客户端：过期页面 | 真实页面等待超过 60 秒票据 TTL 后点击；拒绝回执和 Audit 持久化，无资产/账本/Outbox 变化 | PASS：真实服主客户端 |
| 服务端：持久恢复 | 真实交易后完整重启及 disable/enable；PlayerId/XUID/UUID/firstSeen、职司、账户、原 Receipt 和 Outbox 逐项保持 | PASS：服务器读取；不等同玩家重连 |
| 真实客户端：重连/重启后调用 | 玩家再次认证、重启后 SDK 再次重放原 key | NOT RUN：用户要求转为内部测试后未继续客户端操作 |
| 真实客户端：普通玩家 | 普通玩家拒绝管理、授予/撤权、旧 UI DENIED、OP/Role 分离 | NOT RUN：当前只能安排服主账号 |
| 真实客户端：改名/同名 | 真实认证账号变更和冲突 | NOT RUN：域测试不代替真实账号场景 |

自动化域、Runtime 测试数据库和公开 fixtures 使用合成身份。真实客户端测试产生的身份、SQLite 和日志只留在忽略的私人运行目录；公开报告只发布汇总结果和经过审查的摘要。私人 Owner 配置本身不能证明该玩家已完成认证登录。

Runtime 测试的资产开放入口只在测试程序编译宏中存在；已单独编译生产对象并检查符号，未发现测试 hooks。正式配置的 `validatedAssets=true` 会被当前版本拒绝，不能用配置代替验收。

首次实现 a976fcc 的本地完整构建用时 40.8 秒，完整测试用时 51.2 秒，125 组计数用例通过；对应 [云 CI 37212361400](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/runs/37212361400) 也通过。后续发现公开玩家票据路由和辅助客户端发现缺口，增加 1.2 契约，不能把首次 CI 当新版验收。当前新版完整构建 41.4 秒、测试 57.8 秒，148 组合成用例加 4 组真实 DLL 契约，共 152 组计数测试通过。另五套契约检查也成功，不虚构它们的数量。正式包恰好 40 文件/9 DLL，独立验证包两文件；正式 Core 与 Fixture 均无 LL/BDS 导入，Host 保留官方 memory/load 标记。

新版 [云 CI 37217978119](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/runs/37217978119) 已通过，绑定源码 `8cf831f1e06a7a021492c625071ecf6061808f24`，不能用后续文档提交代表重新编译的 DLL。两个 Artifact 已实际下载并核验；云 DLL 已部署到独立测试 BDS 1.26.51.1 / LeviLamina 26.51.6，完成两次真实启停、首轮 disable/enable、两次 terminal cleanup PASS。汇总见 [云产物证据](phase2-cloud-evidence.json)。测试期间已验证 NetherNet TCP 监听与本机服务器信息接口，协议 2193；这只是网络就绪检查，不能证明真实玩家认证。最终服务器已正常停止。

正式资产 feature bits 为 0。七个业务模块保持 PLANNED / disabled。不把 Cloud CI、Mock、原生编译或域测试当成真实玩家验收。

客户端曾反馈两条资产命令异常，经逐条核对，它们分别为预定的余额不足与同 key 不同金额负向测试；两次拒绝均符合预期。请求重试未增加钱包或账本，余额不足拒绝回执可持久查询；私有数据库 integrity、foreign keys、钱包与账本合计、交易与回执关联均通过。真实测试账户的身份、金额和完整记录不纳入公开证据。

当前只有服主账号。真实测试尚不能证明普通玩家拒绝管理、原生 OP 不自动获得 Eternal 管理权、双人转账或他人撤销职司后的旧页面拒绝。Owner 自身 Builder 授予/撤销不会消除 Owner 权限，不能替代失权测试。Outbox 的 EventBus 通知是 advisory；发布成功不等于消费者 ACK 或恰好一次交付，真实业务消费者去重/ACK 仍 NOT RUN。

用户要求后续由内部测试完成，因此没有继续要求客户端重连或第二账号。现有 Domain 30、Runtime 42、SDK 19 组安全测试复跑，合计 91 组通过，三个进程退出码均为 0；这些仍为合成测试。真实交易后的服务端重启、停用恢复和只读快照比对已通过，身份与账户不变，历史 Receipt 和 Outbox 内容完全一致，消费者保留重试状态。不能将这个结果改写为“客户端重新登录并成功操作”。

临时 CoreValidationModule 已在停服状态撤下，其模块配置和能力批准同时移除；developmentValidation 恢复 false，validatedAssets 仍为 false。正式运行目录仅一个 Host DLL 与八个模块 DLL，只有 Core 启用。云 DLL 摘要未改变；正式目录完成两轮真实 BDS 回归并正常停服，玩家测试数据库保留在私人目录，没有导入旧经济数据或删除历史数据。

### 验收边界

| 用户验收项 | 自动化 / 内部结果 | 真实环境结果 |
|---|---|---|
| 认证身份、稳定 XUID/UUID | 非法输入、唯一约束、冲突、UTF-8 / 大小写、离线查询 PASS | 单个真实 Owner 首次认证 PASS；第二账号/改名/同名冲突 NOT RUN |
| Role / Permission / OP 分离 | 持久 Role、授权与撤销、OP 独立 PASS | Owner 授予 Builder / 持久化且不授予 OP PASS；普通玩家拒绝、OP 不获管理职司 NOT RUN |
| Capability / 可信上下文 | 模块、会话、版本、TTL、撤销及旧 UI PASS | 真实 SDK Invocation / 授权 PASS，过期页面拒绝 PASS；他人撤权旧 UI NOT RUN |
| Money / Reputation / Ledger | 原 17 组与 Phase 2 域原子性、并发、溢出、幂等 PASS | 增减、重复请求、冲突、余额不足和对账 PASS；双人转账 NOT RUN |
| Audit / Receipt | 拒绝策略、有限审计与持久回执 PASS | 资产/Role/冲突/过期审计、成功与拒绝回执、历史内容保持 PASS；真实处罚/高危配置尝试 NOT RUN |
| Outbox | 重放、乱序、失败重试、消费者去重、进程中断恢复 PASS | 事务事件、SDK 查询、重启保持和未 ACK 重试 PASS；真实业务消费者 ACK / 去重 NOT RUN |
| Migration / 恢复 | fresh / upgrade / 失败回滚 / 未知新版拒绝 / backup integrity PASS | 新测试库 schema 2 和交易后正常重启 PASS；真实玩家重连调用 NOT RUN |
| SDK / ABI / CI / Artifact | C11/C++20 布局、1.1 前缀、1.2 契约、云 CI PASS | 下载摘要及云 DLL / 正式目录真实 BDS 生命周期 PASS |

不开放正式资产 feature bits，不进入 Phase 3。物品 Pending/Reconciliation 仍是契约草案，本阶段不包含背包 exactly-once delivery。
