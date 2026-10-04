# 依赖与版本基线

| 组件 | 固定基线 | 用途 |
|---|---|---|
| BDS | 1.26.51.1 | 本地真实运行验证，单独获取，不入仓库/发布包 |
| LeviLamina | 26.51.6 / 32fcaa02baa38371b705358801c7d185c284233e | 唯一引擎加载器；Host 适配 |
| levibuildscript | 0.6.1 | 官方 Host 构建规则 |
| SymbolProvider | 1.3.0 / 6c93ec45c8455992ee726d92df60316c8e731c44 | 官方 Bedrock 延迟导入解析，不以系统 delayimp 替换 |
| LLVM clang-cl | 22.1.0 | Host 原生 MSVC ABI 编译 |
| MSVC CRT/STL | 14.44 固定 payload | ABI/运行库依赖，按官方包条款获取 |
| Windows SDK | 10.0.26100 固定 NuGet payload | Windows x64 头与链接依赖 |
| xmake | 已验证 3.1.1；工程最低3.0.0 | 正式顶层构建入口 |
| SQLite | 3.53.4 | 独立 Core 域/私有数据库 |
| EternalSDK | ABI major 1 | 自研 C/C++ 公共契约，不是运行时插件 |

具体 URL、commit、SHA-256 和校验限制以 docs/toolchain-lock.json 为准。此锁记录已取得的来源，不意味着每个新 Host/模块产物已经构建或验收。VS 顶层 channel manifest 与 CDN 摘要不符的历史限制必须保留；逐包 payload 校验不能替代顶层 manifest 校验。

官方来源：[LeviLamina](https://github.com/LiteLDev/LeviLamina)、[原生模板](https://github.com/LiteLDev/levilamina-mod-template)、[构建规则](https://github.com/LiteLDev/levibuildscript)、[SymbolProvider](https://github.com/LiteLDev/SymbolProvider)、[SQLite](https://www.sqlite.org/download.html)。头依赖来源/版本另由锁文件列出，许可证见 THIRD_PARTY_NOTICES.md。

仅 Host 链接引擎适配；内部模块不能带入 YEssential、LSE/QuickJS/Lua、LegacyMoney、LegacyRemoteCall、PLand 或旧 JS。旧环境可以只读对照数据，不作为新运行时依赖。

升级需一起核对 BDS/LL/工具链/生成头与预链接数据，再跑构建、ABI、Host 生命周期、事件 hooks 和真实客户端协议回归。不承诺任意新版本自动兼容；不从源码发布许可推导 BDS/微软包的再分发权。
