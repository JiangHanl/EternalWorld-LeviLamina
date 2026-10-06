# 测试策略

从 Phase 2 自动化收尾起，日常开发不等待人工 Minecraft 客户端操作。内部验证使用合成身份、临时数据库、真实 DLL、独立真实 BDS 和 GitHub Actions；不引入外部 Bedrock Bot 或协议运行时。

## 分类

| 标记 | 范围 |
|---|---|
| UNIT | 独立函数、SDK DTO 布局、客户端契约 |
| DOMAIN | 真实业务域代码与临时 SQLite，含多连接并发和故障恢复 |
| SYNTHETIC_INTEGRATION | 合成身份、会话、职司、权限、模块与可信入口集成 |
| REAL_DLL | 实际编译 DLL 的发现、调用、消费者、生命周期和生产隔离 |
| REAL_BDS | 实际 BDS / LeviLamina / Host / Core 启停与诊断 |
| CLOUD_CI | GitHub Actions 上的构建、测试、打包与产物扫描 |
| REAL_CLIENT | 真正 Minecraft Bedrock 客户端操作 |

每个用例记录一个分类。一个 CI 工作流可运行前四类用例，但不能把这些结果叫 REAL_CLIENT。合成认证只是受控测试控制器输入；只有引擎认证 Player 才是线上身份来源。REAL_BDS 无玩家运行不证明玩家登录。

## 开发门槛与生产门槛

DOMAIN、SYNTHETIC_INTEGRATION、REAL_DLL、REAL_BDS、CLOUD_CI 与云 Artifact 回归全部通过后，可以标记 **DEVELOPMENT COMPLETE**，继续下一 Phase。未执行的客户端项目保留 **DEFERRED_REAL_CLIENT**；生产状态仍为 **PENDING REAL CLIENT**。生产验证不能由开发完成替代。

开服前人工门槛集中在 [PRE_RELEASE_CHECKLIST.md](PRE_RELEASE_CHECKLIST.md)，只在公开 Beta / 正式开服前安排。历史已经执行的真实客户端结果保留其证据，不追溯补造缺失场景。

## 隔离与自动运行

`SyntheticMultiUserTests` 使用 Owner、Administrator、Builder、Moderator、NormalPlayerA/B、OpOnlyPlayer、RevokedPlayer，临时数据库与受控会话。真实并发发生于 DOMAIN SQLite 多连接；引擎绑定 Runtime 仍拒绝工作线程调用。

`EternalCoreValidation.dll` 是独立编译的验证变体；`EternalTestConsumer.dll` 只通过正式 SDK、Service Registry、EventBus 操作自己的投影数据库。消费者先原子提交副作用与 eventId 去重，再 ACK；测试在两者之间真实退出子进程，以验证故障重放。

正式 Core 不含验证编译宏；修改 JSON 的 `developmentValidation` / `validatedAssets` 不能开启测试资产入口。正式包只有 Host 和八个模块 DLL，测试 DLL、测试导出、私有 Runtime hooks、世界、数据库和日志全部排除。CI 的 **Production Artifact Test Hooks Scan** 检查实际二进制、配置和 ZIP，另用实际生产 DLL 验证配置无法解除限制。

真实 BDS 回归由 `tools/real_bds_smoke.py` 经 stdin 完成启动、状态、自检、停用/恢复、完整重启、停服和终止 cleanup。原始日志与玩家数据只留在忽略的私人测试目录。CI 不下载或启动 BDS，不把云测试当真实 BDS。
