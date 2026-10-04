[English](README_EN.md) | [简体中文](README.md)

# EternalWorld · LeviLamina

A native C++ server project for Minecraft Bedrock Dedicated Server and LeviLamina. The target architecture has one thin loader, **EternalHost**, eight internal native modules, and an EternalSDK exposing a versioned C ABI with C++ helpers. Only the Host integrates with the engine; modules own separate business responsibilities and communicate through published interfaces.

This is a Phase 1.5 development prototype. Local builds of nine DLLs, 23 Host test groups, 17 SQLite domain test groups, and real BDS startup, disable/enable, restart and terminal cleanup have passed. Cloud builds still require independent verification. Asset APIs remain disabled, and seven business modules are legal skeletons only. This is not a complete production survival server.

- [Status](CURRENT_STATUS.md) and [test report](TEST_REPORT.md): verified scope and missing work.
- [Architecture](ARCHITECTURE.md), [build](BUILD.md) and [dependencies](DEPENDENCY.md): boundaries and pinned toolchain.
- [API](API.md), [ABI](ABI.md) and [module development](MODULE_DEVELOPMENT.md): public contracts.
- [Database](DATABASE.md) and [migration](MIGRATION.md): asset authority and recovery.
- [Contributing](CONTRIBUTING.md) and [security](SECURITY.md): development and reporting practices.

The repository excludes server binaries, worlds, player databases, operator identities, credentials and vanilla assets. Public packages contain only reviewed project outputs, configuration examples and license notices. You must obtain runtime dependencies separately under their own terms.

Original project code is available under the [MIT License](LICENSE). Upstream components retain their licenses; see [third-party notices](THIRD_PARTY_NOTICES.md). This project is not affiliated with Mojang, Microsoft or LeviMC.

## Runtime architecture and modules

```text
BDS + LeviLamina
└─ plugins/Eternal/
   ├─ manifest.json → EternalHost.dll
   ├─ modules/ → eight separate DLLs
   └─ config/ data/ logs/ resources/
```

Host handles discovery, dependency resolution, ABI/capability checks, lifecycle, Service Registry, EventBus and fault isolation only. Internal DLLs are not independent LeviLamina NativeMods. Runtime dependencies exclude YEssential, LSE, LegacyMoney, LegacyRemoteCall, iListenAttentively and PLand.

| Module | Responsibility |
|---|---|
| EternalCore | Identity, permissions, authoritative assets, transactions, ledger, audit, receipts and recovery |
| EternalCommerce | Banking, tax, trading, shops and listings |
| EternalLife | Sign-in, progression, task progress and eligibility |
| EternalWorld | Teleportation, protected items, death, PVP, NPCs and land |
| EternalContent | Announcements, commissions and activity definitions |
| EternalManagement | Roles, administration, approvals and enforcement |
| EternalPresentation | UI, themes, HUD, MOTD, poetry and particles |
| EternalEncounters | Bosses, combat attribution and reward requests |

Core is an infrastructure prototype with asset APIs disabled. All seven business modules are **PLANNED / NOT IMPLEMENTED** and disabled by default.

## SDK and development

DLL boundaries use a Stable C ABI; development uses modern C++ EternalSDK. API 1.0 and module ABI 1.0 are independent drafts. Structures carry size and version; capabilities are queried explicitly. No STL objects, exceptions or SQLite handles cross this boundary. Allocators own corresponding deallocation. Use Service Registry for synchronous calls and EventBus for broadcasts, never another module's private source or database.

Start with the [template](templates/EternalModule/README.md), [SDK](sdk/EternalSDK/README.md), [MODULE_DEVELOPMENT](MODULE_DEVELOPMENT.md) and Example Module. Extend an existing domain first; add a DLL target only for a distinct business domain.

## Installation and configuration

1. Obtain BDS **1.26.51.1** and LeviLamina **26.51.6** separately under their own terms.
2. Download `EternalWorld-windows-x64` from a successful [Windows Actions build](https://github.com/JiangHanl/EternalWorld-LeviLamina/actions/workflows/build.yml). Verify the ZIP against its `.sha256`, then place `Eternal/` inside `plugins/`.
3. Copy `config/modules.example.json` to `config/modules.json`. Core is mandatory; keep the seven skeleton modules disabled. Configuration, private data, logs and resources have separate directories.
4. Verify `ecore status`, `ecore selfcheck`, `eternal status` and `ll list`; the latter should list only Eternal. Stop gracefully before replacing DLLs. Preserve existing configuration and data.

The [deployment helper](tools/Deploy-Host.ps1) copies local products and initializes active configuration only when missing. The [example](config/modules.example.json) contains no operator identity.

## Builds, artifacts and releases

GitHub Actions is the primary environment: Windows x64 / Server / Release, LLVM **22.1.0**, XMake **3.1.1**, C++20 and **MD** runtime. Sources and hashes are in the [build lock](tools/build-lock.json). Each DLL has its own target. CI builds all nine DLLs, runs SDK, Core, Host, configuration and actual DLL tests, then creates `EternalWorld-<commit>-windows-x64.zip` and SHA256.

See [BUILD](BUILD.md) for local reproduction and preserved portable tools. CI never runs BDS; real-server acceptance is recorded separately. Artifacts contain only Eternal, project DLLs, configuration examples, build metadata and licenses, without a server, world, database or logs.

The [release workflow](.github/workflows/release.yml) builds, tests, packages and hashes an existing `v0.x.y-alpha.N` tag, then creates a draft prerelease. Metadata records Eternal, API, ABI, LL and BDS compatibility. No stable 1.0 or published release is claimed yet.

Read [CONTRIBUTING](CONTRIBUTING.md) and include reproduction steps and tests. Follow [SECURITY](SECURITY.md) for sensitive reports; never upload player data or credentials. The private Phase 1 rollback tag is intentionally absent from public history.
