# 变更记录

## Unreleased · Phase 2 实现与验收

- 保留 Phase 1.5 源码与 CI 基线，创建本地保护标签；原历史报告不改写。
- 新增编号 002 迁移、稳定身份与显示名版本、独立职司权限、账户版本、持久拒绝回执及有限审计、每消费者 Outbox 与恢复。
- 保留旧 Core API 1.0 / Module ABI 1.0，新增独立 Core API 1.1 与 Host 私有认证接入协议；业务查询只返回绑定真实模块的专属上下文。
- Host 薄适配层复制 BDS 已认证 Player 输入并渲染 Core 验证表单；不通过名字、表单字段或原生 OP 授权。
- 新增 SDK、Runtime、Host bridge 与域回归；具体 PASS / NOT RUN 以 Phase 2 报告为准。生产资产继续关闭，未开始七个业务模块。

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
