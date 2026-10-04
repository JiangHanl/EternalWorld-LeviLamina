# EternalSDK / Module ABI 1.0 / Core API 1.0 + 1.2

SDK 是编译期头文件与本模块内 C++ 辅助，不是运行时插件，不加载 LL、脚本引擎或数据库。EternalHost 是唯一交给 LeviLamina 加载的 DLL；Host 按配置加载八个内部模块。内部模块只使用 SDK 公共接口，不能包含 Host/其他模块私有源码。

目录的权威实现位于 `abi/`、`cpp/`、`Module/`、`Core/`、`Events/`、`UI/`、`Types/`、`DTO/`、`Version/`；`include/EternalSDK/` 只转发标准公开 include 路径。使用 `-I sdk/EternalSDK/include`，例如 `#include <EternalSDK/Module/module_abi.h>`。原 `EternalSDK/abi.h` 仍转发到 Core 原始 C ABI，字段、状态和 100 分 = 1 两语义不变。

模块导出五个 `extern C / __cdecl / noexcept` 函数：`EternalModule_GetDescriptor`、`EternalModule_Load`、`EternalModule_Enable`、`EternalModule_Disable`、`EternalModule_Unload`。描述符由模块静态持有，Host 验证并复制 ID、显示名、semver、ABI、required/optional Host 能力和依赖。Host 生成并绑定当前模块的 opaque context；模块 ID 和能力位均不是玩家授权。

`cpp/ModuleExports.hpp` 提供 `EM_IMPLEMENT_MODULE(Type)`，在模块内部捕获 C++ 异常再返回状态，异常不能越过 DLL 边界。Type 提供静态 descriptor 和五阶段实现；Load 失败之后仍必须允许 Disable/Unload 清理部分状态。原生 DLL 不构成恶意代码/无效指针/系统异常沙箱。

Load 只准备本模块数据。每次 Enable 重新发布服务与事件订阅，Disable 将业务入口切换为不可用。Host 在调用 Disable 前撤销该模块服务和订阅；Unload 才释放模块。重复 Host Enable/Disable/Shutdown 幂等，不重复注册。Enable/Disable/Unload 不可在同步 EventBus 回调中重入。

服务名使用 `<module-id>.` 前缀；`EternalCore.CoreApi` 是 `core` 模块保留兼容名称。消费服务的模块必须声明对应 provider 依赖。服务函数表由 provider 持有，仅其 Enabled 期间有效；消费者 Disable 时丢弃缓存，重新 Enable 后重新查询。Host 不替业务方法做身份或资产授权。

EventBus 在 Host 绑定线程同步派发，topic/data 仅在本次 publish/回调期间借用，消费者需自行复制要保存的数据。订阅 user 由消费者拥有，直到退订完成或 Disable 撤销；Host 不释放该指针。模块只能发布自己 ID 前缀的 topic；Host 的原生事件入口可发布其他 topic。嵌套深度限制 8、单次顶层派发最多 1024 回调、载荷最多 64 KiB。服务和订阅每模块最多 64、全 Host 各最多 256。

HostContext 的结构输入是 Load 当次借用，可复制 POD 字段；instance 和 config/data/resources 目录底层存储属于本次绑定，Unload 后不可调用或保留。配置辅助只形成本模块目录的文件名，不是文件系统沙箱。所有模块均是服务器所有者批准的本机代码。

Core 必须 enabled+required。其他模块可关闭；缺少 optional DLL 安全跳过。enabled optional 模块坏描述符、依赖/能力不足、循环或 Load/Enable 失败时拒绝该模块及依赖它的支路，保留健康 Core；required 模块失败则整 Host 不 Ready。PLANNED 模块不能声明业务已实现。生命周期清理失败时撤销服务并保留 DLL 及其 provider 依赖闭包，禁止冒险 FreeLibrary；Host Native DLL 也必须保持驻留。

Core 原 API 1.0 当前只提供真实 version/features，资产/身份四项接口始终 `EC_UNSUPPORTED`，feature bits 为零。Core 保留直接 `EternalCore_QueryApi` 导出便于 ABI 诊断；业务正常通过 Host 服务注册查询。能力票据 mint/executor 绑定/actor 会话/角色 revision/预算校验未成立前不得打开资产能力。调用者填写 XUID、插件名或“服主”字样没有授权效果。

跨 DLL 仅固定宽度 POD、显式 size/version、UTF-8 字节长度和借用指针；不传 STL/异常/分配器/私有 DB/任意 JSON patch。请求和输出零初始化并填写精确 v1 size/version；reserved 为零。Core table v1.0 为 96 字节，Module descriptor 120、HostContext 144；Windows x64、little endian、pack 8。函数表 major 不匹配拒绝，minor 只允许已有字段尾部追加。

测试从仓库根目录执行：

```powershell
python sdk/EternalSDK/tests/validate_sdk.py --clang <clang-path>
```

静态测试验证 C11/C++20 布局和公开边界，不加载 DLL。HostRuntimeTests 验证 mock 生命周期/注册/事件故障；ModuleArtifactTests 验证真实模块 DLL；真实 LL 启动、命令和停服另行验收。任何一层不能代替下一层。

LL 适配 DLL 须采用匹配版本的官方 `linkrule`、`prelink` 与静态 [SymbolProvider](https://github.com/LiteLDev/SymbolProvider/blob/6c93ec45c8455992ee726d92df60316c8e731c44/src/SymbolProvider.cpp)。SDK-only 内部模块不调用 Bedrock 私有符号，不需要复制 LL NativeMod 注册、内存覆盖或假 `bedrock_runtime.dll`。

Phase 2 的独立 `EternalCore.Phase2Api` 保留旧前缀并增加版本化 DTO，详见 [契约](../../docs/PHASE2_API.md)。模块查询取得 Core 专属上下文，`core.native.*` 不向模块开放。公开服务的生产 feature bits 当前仍为 0；测试专属私有 C++ hooks 不在 SDK 或正式 DLL 中。
