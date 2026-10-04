# Phase 2 验收报告

状态：实现和验收进行中。Phase 1.5 历史记录保持原样，本页只记录 Phase 2。

| 层次 | 当前证据 | 状态 |
|---|---|---|
| 原事务域 | 17 组历史语义回归 | PASS |
| Phase 2 私有域 | 30 组迁移、身份、权限、事务、拒绝回执、Outbox 历史/消费者和真实子进程中断恢复 | PASS |
| SDK 静态契约 | 旧布局保留、新 C11/C++20 DTO 布局及 16 组合成 C++ 客户端契约 | PASS |
| 引擎入口编译 | HostMod、Core Module、LLAdapter 三个 TU 使用固定真实 LL 头独立编译 | PASS |
| Host 模块绑定 | 新 9 组私有隔离、绑定代次、撤销、依赖能力；原 23 组继续通过 | PASS：合成 Host 测试 |
| Runtime | 30 组 Core 票据、过期、旧 UI、预算、回执恢复、输入回归和实际线程退出证明 | PASS：合成 Runtime 测试 |
| 完整九 DLL 构建 | 全部 DLL 链接/导出与输入哈希核对，11 套测试及 4 套双语言布局验证 | PASS：本地固定工具链 |
| 本地 DLL 真实 BDS | 两轮启动、disable/enable、正常停服及新 native status/selfcheck；新库为空、schema 2、外键/账本/回执一致 | PASS：尚无玩家连接 |
| GitHub Actions | 本阶段源码的 Windows 构建和 Artifact | NOT RUN |
| 云 Artifact 真实 BDS | 启停、停用恢复、重启和终止清理 | NOT RUN |
| 真实客户端：服主 | 认证身份、角色、Money/Reputation、幂等、余额不足、重连/重启恢复 | NOT RUN |
| 真实客户端：普通玩家 | 普通玩家拒绝管理、授予/撤权、旧 UI DENIED、OP/Role 分离 | NOT RUN：当前只能安排服主账号 |
| 真实客户端：改名/同名 | 真实认证账号变更和冲突 | NOT RUN：域测试不代替真实账号场景 |

所有测试数据库和公开 fixtures 使用合成身份。真实客户端测试产生的身份、SQLite 和日志只留在忽略的私人运行目录；公开报告只发布汇总结果和经过审查的摘要。

Runtime 测试的资产开放入口只在测试程序编译宏中存在；已单独编译生产对象并检查符号，未发现测试 hooks。正式配置的 `validatedAssets=true` 会被当前版本拒绝，不能用配置代替验收。

本地完整构建用时 40.8 秒，完整测试用时 51.2 秒。明确计数的六套合计 125 组；其他契约和真实 DLL 检查按 suite 单独记录，不虚构用例数量。正式 Core 无 LL/BDS 导入，Host 保留官方 memory/load 标记。本地证据保存在被忽略的 artifacts 目录；这些结果不能代替后续云产物及真实玩家验收。

正式资产 feature bits 为 0。七个业务模块保持 PLANNED / disabled。不把 Cloud CI、Mock、原生编译或域测试当成真实玩家验收。
