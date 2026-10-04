# 本地运行目录

此目录由只读原始参照复制 BDS、PreLoader、基础资源和 LeviLamina 后生成。未复制旧世界和旧业务插件。

新世界 EternalNativeTest；端口19142/19143，当前传输为原配置 NetherNet。运行文件、世界、日志和数据库不纳入 Git。

启动：在此目录运行 .\bedrock_server_mod.exe；退出用 stop，避免强杀。部署 DLL 前必须停服。当前运行目录仅有 LeviLamina 与 Eternal。LeviLamina 只加载 EternalHost.dll，Core 与其他七个 DLL 位于 Eternal/modules，由 Host 管理。

控制台诊断：ecore status、ecore selfcheck、eternal status、ll list。2026-10-04 已通过两次真实启动、健康检查、停用恢复和完整重启，两次 stop 退出码均为 0；两次都确认内部模块已停用并卸载，关闭日志无 Host 错误。测试结束已正常停服。证据见 [Phase 1.5 验收](../docs/phase1.5-evidence.json)。资产 API 尚未开放，其他七个业务模块标记 PLANNED 并默认关闭。

原 Phase 1 的 EternalCore 运行目录完整保留在忽略的 .deps/phase1-baseline-runtime/deployed-EternalCore；本地 phase1-native-core-baseline 标签保留。恢复时先正常停服，将当前 Eternal 移出 plugins，再恢复原 EternalCore 目录；不要同时加载两套入口。原始参照和修改版服务器未改动。
