# 原生构建与发布工具

主构建采用官方 XMake package/linkrule：LLVM 22.1.0、XMake 3.1.1、LeviLamina 26.51.6、BDS 1.26.51.1，依赖锁为 `tools/build-lock.json`。

```powershell
./tools/Prepare-CI.ps1
./tools/Build-Release.ps1
./tools/Test-Release.ps1
./tools/Package-Release.ps1
```

本地已经准备官方 LLVM/MSVC/Windows SDK 便携依赖时，可以执行回归构建：

```powershell
./tools/Build-Portable.ps1
./tools/Test-Portable.ps1
./tools/Package-Release.ps1
```

测试需要真实 Python 运行时；如 `python` 在本机指向 Windows Store 别名，可通过 `-Python <python.exe 路径>` 指定已安装的运行时。

仅 Host 源变更时，可使用 `Build-Portable.ps1 -HostOnly`；工具会拒绝模块源或 DLL 摘要漂移，并在收据中保留原模块构建来源。

首次云构建在上游 `libhat 0.4.0` Git 下载阶段失败。`Prepare-CI.ps1` 现在对锁定官方 recipe 做两处可审计修正：同一 commit 的官方源码 ZIP 与 SHA256 校验，以及 LL 子构建继续使用锁定的官方 recipe 仓库。修改前后 recipe 摘要均写入 `build-lock.json`；构建与 linkrule 仍由官方实现执行。失败诊断保存在 `artifacts/ci/`，CI 不运行服务器。

锁定的 LL recipe 仓库必须先于通用 XMake 仓库：LL 为 `expected-lite v0.8.0` 指定含所需 in-place 重载的官方 commit `f339d2f73730f8fee4412f5e4938717866ecef48`。同名通用 recipe 指向不同源码；优先使用 LL recipe 与已验本地 SDK 保持一致，不修改 LL 头或业务实现。

`Build-Native.ps1` 仅是新便携构建的兼容入口。旧 Core 单独部署和 Phase 0 自动初始化入口已禁用；历史版本保留在本地历史中，不用于当前部署。

9 个真实 DLL 目标为 EternalHost 与 8 个内部模块。输出根为 `bin/Eternal`；仅 Host 使用 LL 原生注册、统一内存算子与官方 SymbolProvider delay resolver。模块使用公开 Eternal ABI，不能链接其他模块的私有实现。EternalCore 复用交易域与私有 SQLite 3.53.4，当前运行状态仍不开放资产业务。

测试包括 SDK 的 C11/C++20 布局、Core 17 组交易域测试、Core API、Host 配置与生命周期、模板合约，以及实际 8 个模块 DLL 的加载/导出检查。测试中的断言保持启用。CI 不启动或分发 BDS；真实服务器测试由本地 `tools/real_bds_smoke.py` 单独执行。

发布 ZIP 只包含 `Eternal/`：Host、8 个内部 DLL、manifest、`config/modules.example.json` 和许可证通知；没有 BDS、LeviLamina、数据、日志或 PDB。首次部署由 `Deploy-Host.ps1` 从示例创建 `config/modules.json`，更新保留已有配置。替换 DLL 前必须正常停服。

GitHub Actions 的 build job 只有 `contents: read`；独立 release job 才有 `contents: write`，只对现有 `v0.x.y-alpha.N` tag 创建草稿预发布。actions 均固定官方已核验的 commit SHA。工作流必须在仓库创建、审核并推送后实际运行；仅本地文件验证不能宣称云端 CI 通过。

便携工具链记录在 `docs/toolchain-lock.json`。没有运行系统安装器；VS 顶层 channel manifest 的 CDN hash 不符限制保留，各 VSIX payload 独立校验匹配。Phase 1 的旧 Core DLL 验收只证明当时的原型，不替代新 Host 架构的实服验收。

