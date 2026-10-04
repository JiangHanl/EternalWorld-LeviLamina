# 公共 API

EternalSDK 提供 C ABI 和 C++ 辅助接口。真实导出与结构定义以 sdk/EternalSDK 的公共头为准；sdk/EternalSDK/include 提供标准 EternalSDK include 路径的转发头。模块内部 Core.hpp 不是第三方业务 API。Phase 2 新增独立 Core 1.1 服务，详见 [Phase 2 契约](docs/PHASE2_API.md)。原 96 字节 API 1.0 和 Module ABI 1.0 保持不变。正式资产能力仍关闭，不能继承 Phase 1.5 的玩家验收结论。

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
