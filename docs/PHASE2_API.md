# Core Phase 2 服务契约

本页描述当前实现，不代表真实客户端验收已完成。验收记录见 `PHASE2_TEST_REPORT.md`。旧 API 1.0 的 96 字节函数表和 Module ABI 1.0 布局不变，旧资产 feature bits 保持 0。新增独立 `EternalCore.Phase2Api` 1.1 服务，240 字节函数表；底层依然是稳定 C ABI。

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

正式能力保持关闭，直到对应真实链路验收通过。开发验证命令是单独的可信引擎入口，不能成为普通模块 API 的降级路径。公开示例的 developmentValidation 和 validatedAssets 均为 false；当前版本拒绝 validatedAssets=true，不允许用配置伪装已验收。

## 原生接入与模块绑定

`core.native.ingress` 是 Host 私有协议，128 字节表，只供薄引擎适配层使用。所有通过模块上下文发起的 `core.native.*` 查询一律拒绝。业务模块不能取得 nonce，不能自造认证玩家或发行 Capability。Host 自己的 C++ 查询入口仍是可信内部接口。

Host 从实际模块描述符和 Enable 代次建立绑定；Core 将声明能力与私有批准清单相交，未批准模块默认拒绝。Host 提供协议上界，实际金额、权限、预算和主体授权仍由 Core 控制。服务返回专属函数表与上下文，不向业务消费者返回未绑定的公共发现表。

Scope 包含模块、Core 实例、玩家在线会话、身份/权限版本、动作、目标、资产、金额和预算。授权票据使用系统随机源，最多有效 60 秒，使用 Core 的单调时钟。Role 撤销、断线、模块停用、Core 停用和过期均在执行入口重新校验。名字只作显示和无歧义目标选择；Owner 不通过名字或 OP 判断。

Host 与模块在同一进程，这不是恶意 DLL 的内存沙箱。运营者仍须只加载可信原生代码。

## 事务与审计

Money 使用 100 分 = 1 两，Reputation 不可转让。请求由 Core 规范化后计算 payload hash，作用域绑定真实 actor 与注册模块。同 key 同内容重放原 Receipt；同 key 异内容返回 CONFLICT 并保留原记录；执行前的当前授权仍然必需。

拒绝请求不写资产账本或成功 outbox。已识别主体的高价值拒绝形成持久拒绝回执；无可信主体时只写有限拒绝审计，不虚构玩家。详细拒绝 Audit 按 actor/module/action/UTC day 限制为 64 条，并持久聚合总数，避免普通错误无限增加详细记录。

成功事务原子写钱包、账本、Audit、Receipt、Outbox。投递至少一次；消费者按 consumer/event 去重，失败或离线保留重试状态。EventBus 广播只含事件 cursor 和 Core 实例版本，不能广播私人身份、余额或授权票据。publish 成功不视为消费者已经确认。

Pending/Reconciliation 仅为后续实物交付契约草案。BDS 背包不在 SQLite 事务内，本阶段没有物品恰好一次交付。

## 配置与生命周期

活动配置位于 `Eternal/config/core/core.json`，公开模板是 `core.example.json`。Owner 为私人稳定 XUID，禁止上传；新测试库从零开始，不导入旧玩家经济数据。没有 Core 配置时只提供健康诊断，不推断首位玩家是服主。配置错误、Owner 不一致或未知数据库版本拒绝启动。

Disable 先关闭适配入口、撤销上下文/票据/待处理表单，再回收服务和订阅。重新 Enable 必须重新发现服务和绑定代次。原 Phase 1.5 的服务器线程与终止清理限制保持不变。
