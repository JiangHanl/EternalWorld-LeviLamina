[English](README_EN.md) | [简体中文](README.md)

# EternalWorld · LeviLamina

A native C++ server project for Minecraft Bedrock Dedicated Server and LeviLamina. The target architecture has one thin loader, **EternalHost**, eight internal native modules, and an EternalSDK exposing a versioned C ABI with C++ helpers. Only the Host integrates with the engine; modules own separate business responsibilities and communicate through published interfaces.

This is a development prototype. The earlier Phase 1 Core DLL passed real BDS loading and lifecycle checks, and the standalone SQLite transaction domain passed 17 test groups. The new Host architecture is Phase 1.5 and remains in progress until its own build, test and runtime evidence is recorded. Player-facing asset APIs remain disabled. This repository is not a complete production survival server.

- [Status](CURRENT_STATUS.md) and [test report](TEST_REPORT.md): verified scope and missing work.
- [Architecture](ARCHITECTURE.md), [build](BUILD.md) and [dependencies](DEPENDENCY.md): boundaries and pinned toolchain.
- [API](API.md), [ABI](ABI.md) and [module development](MODULE_DEVELOPMENT.md): public contracts.
- [Database](DATABASE.md) and [migration](MIGRATION.md): asset authority and recovery.
- [Contributing](CONTRIBUTING.md) and [security](SECURITY.md): development and reporting practices.

The repository excludes server binaries, worlds, player databases, operator identities, credentials and vanilla assets. Public packages contain only reviewed project outputs, configuration examples and license notices. You must obtain runtime dependencies separately under their own terms.

Original project code is available under the [MIT License](LICENSE). Upstream components retain their licenses; see [third-party notices](THIRD_PARTY_NOTICES.md). This project is not affiliated with Mojang, Microsoft or LeviMC.
