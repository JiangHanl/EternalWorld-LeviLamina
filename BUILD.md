# 构建与打包

公开源码不包含 BDS/LL 二进制或工具链。请先核对 DEPENDENCY.md 与 docs/toolchain-lock.json，单独获取固定版本的官方依赖。本工程目标为 Windows x64；源码和脚本使用项目相对路径，不需要运营者的本地目录。

## Phase 1.5 目标流程

在仓库根目录使用 PowerShell。Phase 1.5 的正式构建、测试与安装包已通过实际 GitHub Actions；下载产物的真实BDS生命周期也已通过，详见 TEST_REPORT.md。其他版本和玩家玩法仍需各自验收。

```powershell
& '.\tools\Build-Release.ps1'
& '.\tools\Test-Release.ps1'
& '.\tools\Test-PublicTree.ps1' -Staged
& '.\tools\Package-Release.ps1'
```

Build-Release 以顶层 xmake 为正式构建入口；Prepare-CI 固定官方 LLVM/SQLite 等来源并验证摘要。Build-Portable 是本地官方 LLVM/MSVC payload 环境的回归通道，不能把便携 MinGW 当作 Host 发布编译器。构建/测试脚本的参数和帮助以真实文件为准。

Host 使用官方 LL 版本匹配的 clang-cl/MSVC ABI、C++20、动态 CRT 及 LL 链接规则。内部 DLL 只面向 EternalSDK，不反向链接 Host/Core 的私有实现。SQLite 事务域与接口测试可在无 LL 环境独立执行；它们不替代 Host 真实加载。

## 验证与发布

检查 DLL 导出、导入和实例握手；确保只有 Host 为 LL 插件，内部模块可被 Host 验证且无旧运行时依赖。部署必须停服，不覆盖已加载 DLL。真实 BDS 回归包括加载、诊断、依赖错误、停用/恢复、正常 stop、完整重启；玩家功能另有客户端验收。

运行包使用显式 allowlist，仅包含自研 Host与八个内部DLL、配置示例和许可证通知；SDK公共头与开发说明保留在源码仓库。配置示例在包内 Eternal/config/modules.example.json：Core必需并默认启用，其余业务模块默认停用（EternalCommerce 已进入 Phase 3 实施，其余六个仍 PLANNED）。不要把所有enabled改为true当作业务实现完成。

不得递归复制 server、.deps、世界、玩家数据、日志、运营配置、微软工具链或原版资源。PDB/构建 receipt 保留为本地证据，未审查路径与来源前不进入公开包。

公开前分别扫描 staged tree 与拟推送 public-main：

```powershell
& '.\tools\Test-PublicTree.ps1' -Staged
& '.\tools\Test-PublicTree.ps1' -Treeish public-main
```

扫描通过不代表完整安全审计或许可证合规。必须同时复核第三方声明、公开分支祖先和 CI/真实运行证据。
