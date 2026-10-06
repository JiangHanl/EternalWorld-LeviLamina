# Core Phase 2 服务契约

本页描述当前实现，不代表生产或真实客户端验收已完成。验收记录见 `PHASE2_TEST_REPORT.md`。旧 API 1.0 的 96 字节函数表和 Module ABI 1.0 布局不变，旧资产 feature bits 保持 0。独立 `EternalCore.Phase2Api` 扩展至 1.3：保留完整 240 字节 1.1 与 264 字节 1.2 前缀，尾部追加四个消费者函数，完整表 296 字节；底层依然是稳定 C ABI。

## 公开服务

权威头为 `sdk/EternalSDK/Core/phase2_abi.h`，C++ 包装为 `Core/Phase2Client.hpp`。请求仅携带 Core 签发的 CallerContext、Capability、不透明 PlayerId、requestId、idempotencyKey 和动作参数。请求头的 struct_size 表示整个 DTO 的大小。

| 接口 | 用途 |
|---|---|
| get_phase2_features | 区分已实现、开发验证、正式开放能力 |
| read_identity_v2 / read_roles | 已授权范围内的身份和角色快照 |
| check_permission | 当前主体、目标、动作的服务端权限判断 |
| read_asset | Money 整数分或 Reputation 整数点 |
| submit_mutation | 经 TransactionService 执行增减、转账、角色变更 |
| poll_receipt_v2 | 从持久记录还原结果，不能依赖进程内缓存 |
| read_outbox | 带权限和持久 cursor 的事件读取，不等同消费确认 |
| register_command_route / unregister_command_route | 为当前真实模块绑定登记和撤销受限命令路由 |
| authorize_invocation | 从 Core 签发的真实调用取得限定 Capability；不接受自报主体 |
| register_consumer / query_consumer | 以实际模块与本地 key 建立后台消费者，查询待消费事件与不透明 delivery token |
| ack_consumer_event / retry_consumer_event | 当前消费者与 delivery token 限定的 ACK / 持久重试；不能用猜测 eventId 跨消费者确认 |

正式能力保持关闭。开发资产命令和消费者测试需要独立的编译验证变体；正式 Core 拒绝 developmentValidation=true 与 validatedAssets=true，不能用配置解除隔离。公开示例两值均 false。内部验证完成可推进开发 Phase，生产状态仍受开服前清单约束。

## 原生接入与模块绑定

`core.native.ingress` 是 Host 私有协议，128 字节表，只供薄引擎适配层使用。所有通过模块上下文发起的 `core.native.*` 查询一律拒绝。业务模块不能取得 nonce，不能自造认证玩家或发行 Capability。Host 自己的 C++ 查询入口仍是可信内部接口。

Host 从实际模块描述符和 Enable 代次建立绑定；Core 将声明能力与私有批准清单相交，未批准模块默认拒绝。Host 提供协议上界，实际金额、权限、预算和主体授权仍由 Core 控制。服务返回专属函数表与上下文，不向业务消费者返回未绑定的公共发现表。

客户端发现服务必须明确请求本次 Enable 所需的非零模块能力。路由仅能由相应 CallerContext 登记。真实认证玩家执行 `ecore native invoke <moduleId> <routeId> <arguments>` 时，Core 向该绑定的同步回调交付 Invocation；输入参数不能指定 actor、XUID 或 Owner。公共授权接口从不可猜的 Invocation token 解析主体，再检查路线动作范围、当前角色、会话、模块代次、金额及预算。回调返回后不能再申请新票据；已签发票据继续受有效期与撤销检查约束。

默认 C++ 客户端不使用开发功能位。独立验证模块必须显式选择 DevelopmentValidation，Core 同时检查验证编译宏、私人开发开关和调用授权；客户端选项本身不提供权限。消费者要求 Core 私有批准与 Host 模块能力交集均含 Events 和 Audit，无需伪造在线玩家票据。注册与每次查询 / ACK / retry 仍校验 CallerContext、模块代次与 Core 实例；停止、撤销或重启后须重新发现服务和注册。正式功能位仍为 0，正式包不含验证模块。

后台消费者可持久保存本地 key 和 eventId，不可保存 opaque consumer / delivery handle 或函数表。自己的副作用与去重记录先原子提交，再 ACK；进程在提交与 ACK 之间失败时，Core 重放同 eventId，消费者去重后完成 ACK。EventBus 只提供通知；消费者离线、重复或乱序通知不会替代持久状态。实物交付仍非 SQLite 原子事务，本阶段不承诺物品 exactly-once。

兼容边界：保留 1.1 字段布局不等于旧辅助类二进制可直接运行。早期 1.1 `Phase2Client` 请求零能力并要求表大小恰好为 240，不能正确完成真实绑定。该辅助类须用当前 SDK 重新编译，显式声明所需能力；不为零审批请求增加授权 fallback。正常使用大小/版本检查的非零能力 1.1 C 前缀查询继续由 Host 验证。

Scope 包含模块、Core 实例、玩家在线会话、身份/权限版本、动作、目标、资产、金额和预算。授权票据使用系统随机源，最多有效 60 秒，使用 Core 的单调时钟。Role 撤销、断线、模块停用、Core 停用和过期均在执行入口重新校验。名字只作显示和无歧义目标选择；Owner 不通过名字或 OP 判断。

Host 与模块在同一进程，这不是恶意 DLL 的内存沙箱。运营者仍须只加载可信原生代码。

## 事务与审计

Money 使用 100 分 = 1 两，Reputation 不可转让。请求由 Core 规范化后计算 payload hash，作用域绑定真实 actor 与注册模块。同 key 同内容重放原 Receipt；同 key 异内容返回 CONFLICT 并保留原记录；执行前的当前授权仍然必需。

拒绝请求不写资产账本或成功 outbox。已识别主体的高价值拒绝形成持久拒绝回执；无可信主体时只写有限拒绝审计，不虚构玩家。详细拒绝 Audit 按 actor/module/action/UTC day 限制为 64 条，并持久聚合总数，避免普通错误无限增加详细记录。

成功事务原子写钱包、账本、Audit、Receipt、Outbox。投递至少一次；消费者按 consumer/event 去重，失败或离线保留重试状态。EventBus 广播只含事件 cursor 和 Core 实例版本，不能广播私人身份、余额或授权票据。publish 成功不视为消费者已经确认。

Pending/Reconciliation 仅为后续实物交付契约草案。BDS 背包不在 SQLite 事务内，本阶段没有物品恰好一次交付。

## 配置与生命周期

活动配置位于 `Eternal/config/core/core.json`，公开模板是 `core.example.json`。Owner 为私人稳定 XUID，禁止上传；新测试库从零开始，不导入旧玩家经济数据。没有 Core 配置时只提供健康诊断，不推断首位玩家是服主。配置错误、Owner 不一致或未知数据库版本拒绝启动。

Disable 先关闭适配入口、撤销上下文/票据/路由/待处理表单，再回收服务和订阅。重新 Enable 必须重新发现服务和绑定代次。可信引擎调用持有 Host 的回调保护；回调尚在执行时拒绝重入停用或卸载。原 Phase 1.5 的服务器线程与终止清理限制保持不变。
