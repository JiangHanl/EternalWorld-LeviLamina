# 本地运行目录

此目录由只读原始参照复制 BDS、PreLoader、基础资源和 LeviLamina 后生成。未复制旧世界和旧业务插件。

新世界 EternalNativeTest；端口19142/19143，当前传输为原配置 NetherNet。运行文件、世界、日志和数据库不纳入 Git。

启动：在此目录运行 .\bedrock_server_mod.exe；退出用 stop，避免强杀。部署 DLL 前必须停服。当前仅加载 LeviLamina 与 EternalCore 最小原型，已通过真实加载/健康命令/重启测试，资产和业务功能未开放。

控制台诊断：ecore status、ecore selfcheck、ll list。测试结束已正常停服。
