# Phase 2 验收报告

状态：实现和验收进行中。Phase 1.5 历史记录保持原样，本页只记录 Phase 2。

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
| GitHub Actions | 首次实现 a976fcc 的 CI 已通过；SDK 1.2 路由补充须另跑 | NOT RUN：当前新版 |
| 云 Artifact 真实 BDS | 启停、停用恢复、重启和终止清理 | NOT RUN |
| 真实客户端：服主 | 认证身份、角色、Money/Reputation、幂等、余额不足、重连/重启恢复 | NOT RUN |
| 真实客户端：普通玩家 | 普通玩家拒绝管理、授予/撤权、旧 UI DENIED、OP/Role 分离 | NOT RUN：当前只能安排服主账号 |
| 真实客户端：改名/同名 | 真实认证账号变更和冲突 | NOT RUN：域测试不代替真实账号场景 |

所有测试数据库和公开 fixtures 使用合成身份。真实客户端测试产生的身份、SQLite 和日志只留在忽略的私人运行目录；公开报告只发布汇总结果和经过审查的摘要。

Runtime 测试的资产开放入口只在测试程序编译宏中存在；已单独编译生产对象并检查符号，未发现测试 hooks。正式配置的 `validatedAssets=true` 会被当前版本拒绝，不能用配置代替验收。

首次实现 a976fcc 的本地完整构建用时 40.8 秒，完整测试用时 51.2 秒，125 组计数用例通过；对应 [云 CI 37212361400](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/runs/37212361400) 也通过。后续发现公开玩家票据路由和辅助客户端发现缺口，增加 1.2 契约，不能把首次 CI 当新版验收。当前新版完整构建 41.4 秒、测试 57.8 秒，148 组合成用例加 4 组真实 DLL 契约，共 152 组计数测试通过。另五套契约检查也成功，不虚构它们的数量。正式包恰好 40 文件/9 DLL，独立验证包两文件；正式 Core 与 Fixture 均无 LL/BDS 导入，Host 保留官方 memory/load 标记。本地证据保存在被忽略的 artifacts 目录；这些结果不能代替后续云产物及真实玩家验收。

正式资产 feature bits 为 0。七个业务模块保持 PLANNED / disabled。不把 Cloud CI、Mock、原生编译或域测试当成真实玩家验收。
