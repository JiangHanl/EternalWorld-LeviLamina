# 第三方来源与许可证

项目 LICENSE 的 MIT 仅覆盖自主实现，不改变上游条款。本页根据实际取得的固定来源和许可证原文记录，不把全部生成产物统称 MIT。完整文本保存在 third_party/licenses；仅将文本换行统一为LF，内容不改。来源、原换行版本摘要与公开文本 SHA-256 见该目录 manifest.json，避免Git换行转换使公开克隆校验失配。

## 引擎与编译依赖

| 组件 | 实际声明 | 处理 |
|---|---|---|
| LeviLamina 26.51.6 | README 指定开源部分 LGPL-3.0，附 COPYING.LESSER 和 COPYING | 原文保留；仅作为独立官方依赖，不把闭源部分或生成 MC 头重新授权 |
| 官方 mod template | CC0-1.0 | 原模板 LICENSE 完整保留，注册/构建模式有来源 |
| SymbolProvider 固定 1.3.0 commit | src/SymbolProvider.cpp 原头声明 Public Domain，源自 mingw-w64/Microsoft delay helper，引用 DISCLAIMER.PD | 保留原完整声明及 mingw-w64 免责声明；该包没有单独根 LICENSE，不写成 MIT |
| fmt 11.2.0 | MIT，另有可选嵌入例外 | 保留完整 LICENSE，Host 构建静态编入 format.cc 时包含通知 |
| libhat 0.4.0，commit 7375873e560f46e8569c6a389c6077f4c7133089 | MIT，Copyright 2022–2024 Brady Hahn | 保留固定官方完整LICENSE；属于LeviLamina Windows传递构建依赖，不因Host未直接include而省略来源审查 |
| SQLite 3.53.4 amalgamation | 原头 copyright disclaimer/blessing，官方说明为公版 | 保留固定源码原头声明与官方来源，不杜撰 MIT |

固定上游：[LeviLamina](https://github.com/LiteLDev/LeviLamina/tree/v26.51.6)、[模板](https://github.com/LiteLDev/levilamina-mod-template)、[SymbolProvider 原文件](https://github.com/LiteLDev/SymbolProvider/blob/6c93ec45c8455992ee726d92df60316c8e731c44/src/SymbolProvider.cpp)、[fmt](https://github.com/fmtlib/fmt/tree/11.2.0)、[SQLite 版权声明](https://www.sqlite.org/copyright.html)。版本/摘要仍以工具链锁为准。

libhat固定来源：[完整许可证](https://github.com/BasedInc/libhat/blob/7375873e560f46e8569c6a389c6077f4c7133089/LICENSE)。官方LL源码的Windows Signature.cpp包含libhat.hpp，Windows构建声明libhat 0.4.0并将其静态编入LL；当前本地Host未直接包含或链接libhat，公开运行包也不分发LL或libhat二进制。完整MIT文本与归档摘要仍纳入本工程通知，未来若Host头或产物带入其代码，不遗漏上游声明。当前共保存23份许可证/完整notice，原22份记录不变。

SymbolProvider 的公版声明只说明已核验原文件；若以后增加其他文件，必须逐项检查，不继承此结论。分发含 LGPL 库/模板实现的组合产物时须保留上游通知、对应源码/重建信息等适用材料；项目 MIT 不替代这些条款。当前仓库没有把 LL 源码或二进制 vendoring 进公开树。

## 已取得的头依赖

这些依赖在本地 .deps 中由固定源码包提供，不直接复制到公开源码；为构建/产物审查保留取得版本的完整许可证。

| 依赖 | 许可证文件中的声明 |
|---|---|
| concurrentqueue 1.0.4 | BSD-2-Clause；lightweightsemaphore 含单独 zlib 声明 |
| debug_assert 1.3.4 | MIT |
| EnTT 4.0.0 | MIT |
| expected-lite 固定 commit | BSL-1.0 |
| GLM 1.0.1 | Happy Bunny 或 MIT 双选；保留完整 copying.txt |
| Microsoft GSL 4.2.0 | MIT |
| nlohmann/json 3.12.0 | MIT |
| LevelDB 1.23 | BSD-3-Clause |
| magic_enum 0.9.7 | MIT |
| parallel-hashmap 2.0.0 | Apache-2.0 |
| Boost.PFR 固定 commit | BSL-1.0 |
| RapidJSON 固定 commit | MIT，文件另列 msinttypes BSD 与 bin/jsonchecker 的 JSON License |
| stb 固定 commit | MIT 或公版双选，保留完整双许可 |
| type_safe 0.2.4 | MIT |

RapidJSON 的 bin/jsonchecker 测试资源不属于本工程使用/公开包范围，不能把整个上游树都说成 MIT。concurrentqueue 的 benchmark 外部组件也不包含在公开包；若改变打包范围须重新检查。工具链自带 LLVM/MinGW/MSVC/Windows SDK 的条款由各自包控制，公开包不分发它们。

## 不包含与发布门槛

BDS、微软运行/开发包、原版世界/资源、Bedrock runtime symbol data 与权限不明的生成内容不纳入源码或发布包。levibuildscript/prelink 为外部构建工具，当前取得源码/二进制没有足以将全部内容重许可的根许可证材料；不得作为自研 MIT 源码 vendoring 或直接塞入发布包。

公开前核对新增源码是否包含复制实现、发布包是否带入第三方对象/资源及通知是否齐全。无法确认来源或再分发许可的内容须排除，并记录为阻塞项，不通过改上游许可证解决。
