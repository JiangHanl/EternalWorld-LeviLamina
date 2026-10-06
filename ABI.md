# DLL ABI

目标为 Windows x64，C++20 内部实现，跨 DLL 使用 extern C、__cdecl 和不抛出异常的入口。公共结构只使用固定宽度整数、显式 size/version、UTF-8 字节长度、POD 与调用方缓冲区。公开头是契约；本页不扩展未实现字段。

必须禁止：STL 容器/字符串、C++ 异常、CRT 分配器、跨 DLL 释放、SQLite 句柄、LL/BDS 对象或业务私有类跨边界。C++ 辅助层只在调用者内部运行。内存由分配者负责；输出使用调用方提供的容量并报告所需字节数，终止符和缓冲区规则按相应接口定义处理。

| 校验 | 处理 |
|---|---|
| major 不匹配 | 拒绝使用服务 |
| minor/size 不足 | 不访问不存在的字段 |
| 未实现能力 | 不置 bit，返回 unsupported |
| ID/版本/UTF-8/依赖无效 | 拒绝模块加载或启用 |
| required 依赖缺失/环 | 拒绝相关模块；不静默跳过 |
| 停用 | 撤销服务与订阅，停止工作；再次启用时重新注册 |
| 卸载 | 排空回调并撤销实例上下文，释放本模块资源 |

Module ABI 的五个导出名固定，描述符数据寿命必须符合接口；Host 按实际函数指针类型调用。Host 生成的实例上下文只标识模块归属，不能由外部填写，也不能替代 Core 玩家/奖励授权。

借用数据不会转移所有权，跨 DLL 的裸指针必须遵守以下有效期：

| 数据 | 有效期与使用规则 |
|---|---|
| 描述符、依赖及其字符串 | 模块提供静态存储，保持至卸载；Host 复制所需内容，消费者不得释放 |
| Load 的 EmHostContext 指针 | 只在 Load 调用内借用；模块可复制已验证字段，目录字符串和 instance 的底层存储属于 Host，本次绑定至 Unload 返回前有效 |
| 服务表和函数指针 | 提供者拥有，仅提供者启用期间有效；停用后立即丢弃引用，再次启用时重新查询，generation 不能延长寿命 |
| EmEvent、topic 和 data | 只在同步 publish 调用及其当前回调内借用；需要稍后处理时，由消费者在回调内按已验证长度复制 |
| EmSubscription.user | 消费者拥有；保持至成功退订且使用它的回调结束，或 Disable 完成并确认订阅已撤销；Host 不负责释放 |

Unload 后不得使用已复制的上下文字段或 instance 调用 Host。当前事件传输在 Host 线程同步执行，Host 拒绝在回调分发期间停用或卸载；这不提供异步工作队列。模块仍须在释放自己的资源前停止其工作、撤销订阅，并清除服务引用。服务发现和事件传输均不授予 Core 资产权限。

线程绑定由Host适配层管理：首次Load在LL启动线程完成后，仅在首次Enable尝试前且Host完全静止时，显式交接一次至服务器线程。旧线程必须停止调用，且没有已注册服务、订阅或进行中的回调；该交接不是公共SDK能力。运行期间所有生命周期、服务和事件调用固定在线程上，拒绝旧线程和再次启动交接，Enable失败或普通Disable后也不能换线。

唯一最终例外是终止停服。Host保存服务器线程的Windows SYNCHRONIZE句柄，仅WaitForSingleObject(handle, 0)确认线程已退出后，才可在关闭线程执行最终Disable/Unload；之后永久禁止Load/Enable。模块必须允许此最终清理在关闭线程释放私有资源，但不得访问已退出的游戏线程对象。普通Disable仍受原线程限制；Stopping标志只用于延后关闭请求，不能代替线程退出证明。适配层在typed leaveGameSync的origin返回后再调用终止检查。这是Host私有适配接口，未更改公共SDK布局或授予模块换线能力；新路径的mock验证不等同真实BDS停服通过。

验证分为 C11 与 C++20 头/布局编译、真实 DLL 导出和调用、版本/结构大小错误测试、停用/卸载和完整 BDS 重启。任一层通过都不能代替其他层。便携 MinGW 只用于无 LL 的域/契约测试，不用于最终 LL Host DLL。

Phase 2 保留上述旧布局，独立 Core API 1.3 为 296 字节表，前 240 / 264 字节保留完整 1.1 / 1.2 契约。新增路由、Invocation 和消费者仍使用 POD、size/version 和 Core 专属不透明票据；尾部追加四个 consumer 函数，不改变 ABI major。Host 分别检查请求 minor 所需最小长度、provider 实际 minor 和完整函数指针尾部，旧 provider 不能满足新 consumer 请求。128 字节 native ingress 属于 Host 私有协议，模块查询路径禁止访问；公开模块从真实调用路由取得授权，不能直接伪造身份。详见 [契约](docs/PHASE2_API.md) 和权威 [头文件](sdk/EternalSDK/Core/phase2_abi.h)。合成测试专属的私有 C++ hooks 不在 SDK、运行配置、正式 DLL 或服务表中。

验证变体通过 ETERNAL_CORE_VALIDATION_BUILD 编译隔离，测试 Runtime hooks 则另用 ETERNAL_CORE_RUNTIME_TESTING，后者禁止进入任何模块 DLL。正式 Core 无验证 marker / 消费者测试控制导出，配置不能开启验证入口。测试工具通过独立 Artifact 分发；正式 ZIP 的实际二进制及内容扫描是发布门槛。
