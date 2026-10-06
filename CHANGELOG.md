# 变更记录

## Unreleased · Phase 3 Commerce

- `EternalCommerce` 从 PLANNED 桩升级为真实模块，声明 Core 依赖与能力，实现 Load/Enable/Disable/Unload 与 Core 服务发现。
- 五个业务域与编号迁移：伴礼（3—20 人、10 两起、2% 费、300 秒）、转账税（每日免税 1000 两 + 分档税率）、寄售（并发购买、7 天退回、NBT/中文名）、收购（每日配额 + 衰减）、交付 Pending/Reconciliation。
- Core 授权写入闭环：transfer / gift / list / buy 四条 Invocation 路由 + 交付后台消费者（register/query/ack/retry + eventId 去重）。
- 验证变体 `EternalCommerceValidation.dll`；生产 feature bits 保持 0，资产变更只在验证构建下经 REAL_DLL 测试。
- 23 组域测试 + 4 组 REAL_DLL 闭环测试，本地便携套件与云 CI 全绿。详见 [Phase 3 计划](docs/PHASE3_PLAN.md) 与 [交接文档](docs/HANDOFF.md)。

## Unreleased · Phase 2 实现与验收（DEVELOPMENT COMPLETE）

- 保留 Phase 1.5 源码与 CI 基线，创建本地保护标签；原历史报告不改写。
- 新增编号 002 迁移、稳定身份与显示名版本、独立职司权限、账户版本、持久拒绝回执及有限审计、每消费者 Outbox 与恢复。
- 保留旧 Core API 1.0 / Module ABI 1.0，独立 Core API 1.2 保留 1.1 前缀并新增可信 Invocation 路由；Host 私有认证接入不向业务模块公开，业务查询只返回绑定真实模块的专属上下文。
- Host 薄适配层复制 BDS 已认证 Player 输入并渲染 Core 验证表单；不通过名字、表单字段或原生 OP 授权。
- 新增 SDK、Runtime、Host bridge 与域回归；具体 PASS / NOT RUN 以 Phase 2 报告为准。生产资产继续关闭。
- SDK 1.3 云 CI 通过：八身份合成集成、真实 DLL 消费者/崩溃重放与生产隔离；两个 Artifact 已下载核验，云正式 DLL 通过两轮真实 BDS 生命周期回归；真实玩家验收另行记录。
- 真实服主完成资产/幂等/拒绝回执、公开 SDK 与过期页面验收；内部 91 组安全回归与真实交易后服务器恢复通过。临时 Fixture 已撤下、开发资产入口关闭；未取得证据的真实多账号/重连/消费者项目保持 NOT RUN。生产状态为 PENDING REAL CLIENT。

## Unreleased · Phase 1.5工程化验收完成

- 转为一个EternalHost LL插件与八个内部原生模块，统一公开EternalSDK C/C++接口；完成模块握手、依赖/生命周期、服务注册、同步事件传输和构建/Artifact流程。
- 本地九DLL、七个console、23组Host mock、17组SQLite事务域及Core/Module C11/C++20契约验证通过。
- Windows云CI37199429155对源码93dbe05eba4410b73c5c9ce27e16888949e49d58通过，生成39条allowlist、九DLL的Artifact；实际云产物完成两次真实BDS启动、停用恢复、自检、重启及terminal cleanup验收。
- 修复manifest错误加载器依赖、首次Load/Enable线程不同和最终stop换线程问题；停服验收要求明确cleanup PASS且无关闭错误，不能只看退出码。
- 云CI首轮37193348084因libhat官方依赖下载失败，固定官方归档及摘要后重跑通过；失败历史保留。
- 整理中英文入口、数据/ABI/安全文档与第三方许可证，清理公开树中的私人身份/路径；私人历史保留本地。
- 七个业务模块仍PLANNED且默认禁用，Core资产API返回UNSUPPORTED；Phase2和完整玩法未实现，尚未发布正式Release。

## 2026-10-04 · Phase 1本地基线

- 固定BDS1.26.51.1与LL26.51.6，隔离新世界，未导入旧玩家资产。
- 最小Core原生DLL及SDK版本/能力诊断完成真实加载、停用/恢复、完整重启与正常停服验证。
- 修复遗漏官方SymbolProvider导致的首命令崩溃；失败和修复保留于验收摘要。
- 独立SQLite域17组测试通过，包括可信主体作用域幂等、XUID/UTF-8边界、备份与子进程退出恢复。
- 域未接入真实玩家资产或外部能力链；此结果不能代替Phase1.5 Host验收。
