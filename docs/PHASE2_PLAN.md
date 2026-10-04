# Phase 2：永恒核心实施与验收

## 基线与范围

绿色源码为 `93dbe05eba4410b73c5c9ce27e16888949e49d58`，Windows CI 为 [37199429155](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/runs/37199429155)。本地标签 `phase1.5-host-modules-ci-baseline` 指向该公开提交。原 Phase 1.5 报告和证据不改写；本阶段另写报告。

只实现 Core 的身份、职司、权限、Capability、Money/Reputation、事务、账本、审计、回执、Outbox 与恢复。七个业务模块继续 PLANNED / disabled；物品交付只定义 Pending/Reconciliation 状态，不宣称背包恰好一次。

## 实施顺序

1. 持久域：编号 002 migration；保留 001 校验及原 17 组语义。验证 fresh/upgrade/失败回滚/未知新版拒绝/备份、身份冲突、资产原子性、拒绝审计与 Outbox 消费者状态。
2. 公共契约：保留原 96 字节 Core API 与 Module ABI，新增独立 Core 1.1 服务及 POD DTO。跨 DLL 不传 STL、异常、私有类或 SQLite handle。
3. 可信接入：Host 只复制认证引擎输入和渲染通用诊断表单，Core 决定身份、授权及资产行为。Host 注册的模块查询取得 Core 专属上下文，公共查询拒绝私有 native ingress。
4. 本地独立测试、公开索引/许可证审查、稳定提交、GitHub Actions、下载云 Artifact、独立 BDS 启停与真实玩家验收。

## 信任与数据所有权

认证只接受在线非模拟 Player 的引擎认证组件，要求 Full authentication；使用 trusted XUID / authenticated UUID，客户端 UUID 同时交给 Core 做一致性检查。显示名来自引擎，仅用于显示，不作为主键或 Owner 判断。命令只接受真实 Player origin 或明确的 DedicatedServer origin；不把 execute/virtual origin、参数姓名或 JSON 身份当作认证。没有 OP 到 Role 的映射。

Core 私有配置持有既有 Owner XUID，Core SQLite 独占玩家、Role、权限版本、账户、事务、账本、Audit、Receipt 与 Outbox。运行配置和真实玩家身份不公开。公开示例 Owner 留空；缺少配置时安全停留在诊断状态，不猜测服主、不自动选首位玩家。

CallerContext 和 Capability 均为 Core 签发的不透明随机值。上下文绑定已注册模块、模块代次、Core 实例代次、在线会话、主体、动作、目标、资产、预算、过期时间和权限版本。请求执行时重新检查，不接受自报 moduleId / Owner / OP。Disable、断线、重连、撤权或过期使旧上下文失效；正式资产 feature 在对应真实链路通过前保持关闭。

模块声明的服务需求只说明接口依赖，Core 配置另行批准可用能力。模块配置、身份会话和权限必须同时满足；缺失时拒绝服务或方法，不回退读取私有数据库。原生 DLL 是运营者批准的本机代码，这些接口不构成恶意代码内存沙箱。

## 事务与失败语义

Money 使用整数分，100 分为一两；Reputation 使用整数且不允许玩家转让。所有增减经过 Core TransactionService；原子提交 transactionId、幂等 key、payload hash、wallet、ledger、audit、receipt 和 outbox。请求作用域绑定经验证的 actor 与模块，业务方不能选任意 namespace。

授权先于重放；有效且仍获授权的同 key 同请求返回原 Receipt，同 key 不同请求 CONFLICT。旧页面撤权后即使携带以前的 key 也不能继续获得执行授权。拒绝不产生账本或余额半提交；高价值拒绝记录独立持久结果，重复无效输入按明确的窗口聚合，不能无限放大磁盘写入。

提交后由 Core 调度 Outbox，以 eventId 按消费者去重并记录失败/重试。没有消费者时保留待投递，不能把 EventBus publish 返回成功视为消费完成。消费者确认和全局兼容诊断分开；重启仍可读取待处理事件。SQLite 与 Minecraft 背包没有共同事务，本阶段不做物品发放。

## 验收分层

独立 synthetic 测试证明域和 ABI 行为；GitHub CI 证明固定工具链构建、测试和打包；真实 BDS 回归证明云 DLL 的加载与生命周期；真实客户端证明认证 Player 接入、OP/Role 分离、权限撤销和资产重启保持。各层单独记 PASS / NOT RUN / BLOCKED，不能互相替代。

用户已确认可配合真实客户端测试。Owner 账号和普通账号分别验证，当前未执行项不记 PASS。需要真实操作的诊断表单只用于 Core 授权测试，不启用 Management 等业务模块。

## 回滚

停服后使用已验收的 Phase 1.5 ZIP 恢复 Host 与八模块；不热覆盖已加载 DLL。Phase 2 数据库保留原文件及编号迁移前备份。Phase 1.5 不认识新版 schema，不能用旧 DLL 打开新版数据库；回滚到完整旧数据库备份或隔离新数据库。旧参照服务器保持只读。
