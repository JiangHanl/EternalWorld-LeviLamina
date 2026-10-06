# 公共 API

EternalSDK 提供 C ABI 和 C++ 辅助接口。真实导出与结构定义以 sdk/EternalSDK 的公共头为准；sdk/EternalSDK/include 提供标准 EternalSDK include 路径的转发头。模块内部 Core.hpp 不是第三方业务 API。Phase 2 的独立 Core 1.3 服务为 296 字节，详见 [Phase 2 契约](docs/PHASE2_API.md)。原 96 字节 API 1.0、Module ABI 1.0、240 字节 Core 1.1 与 264 字节 1.2 前缀保持不变。正式资产能力仍关闭，不能继承 Phase 1.5 的玩家验收结论。

1.3 追加后台消费者登记、查询、ACK、retry，走真实模块 CallerContext 与 Core 批准的 Events / Audit 权限。事件和 delivery token 绑定当前消费者、模块代次、Core 实例；不能依据猜测 eventId 确认其他消费者的记录。消费者只操作自己的去重与投影数据，先提交副作用，再 ACK。重复、离线、乱序与崩溃重放是正式 SDK 契约，Host 通知本身不是 ACK。

| 层 | 接口用途 | 安全边界 |
|---|---|---|
| Module | 描述符、Load/Enable/Disable/Unload | Host 绑定实例，严格版本/依赖/能力检查 |
| Host services/events | 发布/查询服务、订阅/发布事件 | 实例归属和生命周期回收，不等同资产授权 |
| Core | 身份、权限、事务、回执与快照 | Core 签发 actor/scope/capability，当前授权在执行时重检 |
| Events/UI/DTO | 公共事件、表单和数据契约 | 只承诺头中真实提供的能力 |
| C++ helpers | 类型化调用与 RAII 辅助 | 包装 C ABI，不在 DLL 间传 STL |

模块描述符声明 ID、显示名、语义版本、ABI、required/provided capabilities、required/optional dependencies 和 PLANNED 标记。五个入口为 EternalModule_GetDescriptor、EternalModule_Load、EternalModule_Enable、EternalModule_Disable、EternalModule_Unload。调用约定与字段以 ABI.md 和实际头为准。

消费者先检查服务 major、结构 size/version 和能力位。功能未实现时，不设置该能力位，并返回 unsupported；不能把诊断结果、空壳服务或随机字符串当作已授权功能。调用者上下文不得从命令参数里的 XUID、玩家名、provider/source 名称推导。

资产请求使用稳定幂等键，规范化 payload 与可信主体/执行域绑定；同作用域同键同内容返回持久原回执，同键异内容拒绝。Phase 2 的高价值拒绝请求也形成持久回执与有限审计，成功和拒绝均保留当次请求与版本快照；真实玩家和生产开放状态单独记录在 [Phase 2 报告](docs/PHASE2_TEST_REPORT.md)。物品交付仍未实现。

提供者停用后服务引用立即失效，消费者停止调用并丢弃缓存；再次启用后重新查询。卸载后不得使用先前的函数表和回调地址。Phase 1 的 Core 热卸载按设计未开放；Host 架构只有通过真实卸载/回调排空验证后才能扩大支持范围。
