# 公开 Beta / 正式开服前验收

这份清单是最终人工门槛，不阻塞日常 Phase 开发。未运行的 REAL_CLIENT 项保持 **DEFERRED_REAL_CLIENT**；即使内部测试全部通过，也不能标记 PRODUCTION VALIDATED。仅在准备公开 Beta / 正式开服时统一安排一次，后续客户端相关变更重测受影响项目。

| 分类 | 项目 | 当前状态 |
|---|---|---|
| REAL_CLIENT | 第二个真实玩家认证、独立 XUID / UUID 与重连 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 普通玩家拒绝管理，真实 OP 不获 Eternal Role，Role 不授予 OP | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 双人真实转账、重复请求、双方余额与回执 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 他人撤权后的旧管理表单立即拒绝 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 真实改名与稳定身份、授权、资产保持 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 重启后真实登录与旧幂等 key 重放 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 客户端 UI、HUD、按钮颜色、返回层级和移动端体验 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 服务器列表 MOTD 渐变 / 轮替与聊天展示 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 粒子视觉、视野遮挡、Boss 实战 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 指南针 / 时钟移动、死亡保留、丢弃与容器限制 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 宅地真实建造、箱子、机关、跨界活塞、防火与防爆 | DEFERRED_REAL_CLIENT |
| REAL_CLIENT | 实物交付、背包满、掉线 / 崩溃恢复、邮件与补偿 | DEFERRED_REAL_CLIENT |
| REAL_BDS | 正式候选 Artifact、重启、负载、TPS、实际延迟 | NOT RUN：需最终候选版本 |

已执行的单服主 Phase 2 登录、资产、SDK 路由与过期页面测试见 [历史报告](docs/PHASE2_TEST_REPORT.md)。它们不能替代第二玩家、改名或视觉项目。尚未实现的玩法按后续阶段完成后再验收，不提前宣称通过。

发布前同时检查公开树、许可证、固定依赖版本、Artifact 摘要、生产测试入口扫描、数据库编号 Migration、恢复流程和私人数据隔离。真实世界 / 玩家数据只在私人部署环境处理。
