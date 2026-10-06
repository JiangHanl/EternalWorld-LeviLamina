# 里程碑交接：Phase 3 Commerce 收尾前

本文档面向接手本仓库的后续开发（人或模型）。记录当前完成状态、必须遵守的工作流、以及剩余工作。

## 1. 完成状态总览

| 阶段 | 状态 |
|---|---|
| Phase 1（最小 Core/SDK） | ✅ 完成 |
| Phase 1.5（Host + 八模块 + SDK + CI + 实服） | ✅ 完成 |
| Phase 2（身份/权限/资产/Outbox） | ✅ DEVELOPMENT COMPLETE |
| Phase 3（Commerce） | 🔶 约 60% |
| Phase 4–10 | ⬜ 未开始 |

### Commerce 已完成

- 五个业务域（modules/EternalCommerce/domain/）：伴礼、转账税、寄售、收购、交付 Pending/Reconciliation。
- 迁移 migrations/EternalCommerce/001..005。
- 真实模块 ABI（modules/EternalCommerce/Module.cpp，去掉 PLANNED，声明 Core 依赖与能力）。
- Core 授权写入闭环：transfer、gift、list、buy 四条路由 + 交付后台消费者（register/query/ack/retry + eventId 去重）。
- 验证变体 EternalCommerceValidation.dll（ETERNAL_COMMERCE_VALIDATION_BUILD 门控 setDevelopmentValidation(true)）。

### Commerce 未完成（明确剩余）

1. 收购 Core 接入：系统付款给玩家，需服主审批/授权模型，暂未实现（域逻辑已就绪）。
2. 钱庄：需求过模糊（存取/兑换），需 v3.3.3 产品规格。
3. Commerce 云 Artifact 下载核验 + 真实 BDS 回归（尚未跑）。

## 2. 已验证的成果

- Commerce 域测试 tests/EternalCommerce/commerce_tests.cpp：23 组 DOMAIN。
- 真实 DLL 测试 tests/Host/commerce_module_tests.cpp：4 组 REAL_DLL（transfer 闭环、交付消费者、gift 闭环、consignment buy 闭环）。
- 本地便携套件 Test-Portable.ps1 全量 TEST_EXIT=0。
- 公开树审计 Test-PublicTree.ps1 PASS（218 文件）。
- 云 CI 最新一次 success（origin/main 提交 7181a7c）。

## 3. 构建与测试

### 本地便携构建（clang-cl + MSVC payloads + WinSDK，无需 xmake/VS）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-Portable.ps1
```

### 本地全量测试（务必用 pwsh，不是 powershell，否则 switch/splat 有坑）

```powershell
$py = '<bundled-python-path>'
pwsh -NoProfile -Command "& './tools/Test-Portable.ps1' -Python '$py'"
```

如果机器上有 python，也可直接 Test-Portable.ps1 -Python python；否则用工作区依赖里捆绑的 Python（可用 load_workspace_dependencies 工具定位，替换上面的 <bundled-python-path>）。

### 云 CI

GitHub Actions（.github/workflows/build.yml），push 到 main 自动触发；纯文档提交加 [skip ci]。

### 公开树审计

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Test-PublicTree.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Test-PublicTree.ps1 -WorkingTree
```

## 4. 工作流（必须遵守）

本项目有私有 / 公开两条平行历史：

- 私有分支 main：作者 EternalWorld（近期）/ Codex（早期），完整开发历史，含私有 baseline 标签。
- 公开分支 public-main → origin/main：作者 江寒 <1467313589@qq.com>，净化的线性历史，不含私有历史/标签/凭据。

两条历史内容一致（tree 相同），但 commit SHA 不同。绝不能 git push origin main（会把私有历史推到公开仓库）。

### 正确发布流程（每批）

```powershell
# 1) 在私有 main 上提交
git add -A
git -c user.name="EternalWorld" -c user.email="noreply@users.noreply.github.com" commit -m "..."

# 2) 用 commit-tree 把 tree 镜像到 public-main（作者江寒），快进推送
$tree = (git rev-parse 'main^{tree}').Trim()
$env:GIT_AUTHOR_NAME = '江寒'
$env:GIT_AUTHOR_EMAIL = '1467313589@qq.com'
$env:GIT_COMMITTER_NAME = '江寒'
$env:GIT_COMMITTER_EMAIL = '1467313589@qq.com'
$c = (git commit-tree $tree -p public-main -m "...").Trim()
git update-ref refs/heads/public-main $c
git push origin public-main:main
```

- 每个子项 = 一个提交；一批可多个提交（每个都走上面 commit-tree 镜像）。
- 推送前先跑 Test-Portable.ps1（本地全绿）+ Test-PublicTree.ps1 -WorkingTree（审计 PASS）。
- 网络抖动时 git push 偶发 Connection was reset，重试即可。

## 5. 关键约束与陷阱

- 生产 feature bits 仍为 0：生产 Core 的资产变更返回 EC_UNSUPPORTED。真实资产/转账只在验证构建（EternalCoreValidation.dll + EternalCommerceValidation.dll，配置 developmentValidation=true）下用 REAL_DLL 测试。生产包仍保持资产 feature 关闭。
- 业务模块只通过 EternalCore.Phase2Api（SDK 1.3）+ 公开 EventBus，禁止查 core.native.*、自报 actor/module。
- 迁移 SQL 必须同时落在 migrations/EternalCommerce/00X_*.sql 和 domain/Schema.hpp 的 schemaV*，且字节一致（测试会比对）；用 LF 行尾、首尾各一个空行（与 Core 约定一致）。
- EcUtf8View（Core ABI）与 EmUtf8View（Module ABI）是不同类型；Module.cpp 里 view() 返回 EmUtf8View，路由/消费者字段要用本地 ecView()（返回 EcUtf8View）。
- std::filesystem::path::u8string() 返回 std::u8string（char8_t），传给 std::string 前要 reinterpret_cast<const char*> + size() 转换。
- 测试先行：先写 domain 测试再实现，tools/test-suites.json 登记分类；验证 DLL 不进正式 ZIP。

## 6. 剩余工作路线

### Commerce 收尾

1. 收购 Core 接入（设计系统→玩家付款的授权路径）。
2. 钱庄（先补 v3.3.3 规格再实现）。
3. 云 Artifact 下载核验 + 真实 BDS 两轮回归（复用 tools/real_bds_smoke.py，验证 Commerce 真实模块正常加载）。

### 后续阶段（按 ROADMAP）

Phase 4 Life、5 World、6 Content、7 Management、8 Presentation、9 Encounters、10 迁移/全系统验收。每个阶段照 docs/PHASE3_PLAN.md 的模式：先计划 → 域 + 测试 → 真实模块 ABI → Core 接入 → CI/Artifact/BDS 回归。

## 7. 阅读顺序

ARCHITECTURE.md → docs/PHASE2_API.md（Core 契约）→ MODULE_DEVELOPMENT.md → docs/PHASE3_PLAN.md → TESTING.md → PRE_RELEASE_CHECKLIST.md。
