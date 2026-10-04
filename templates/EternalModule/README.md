# Eternal Module 示例

此示例只演示描述符、生命周期、日志、自有配置、服务发现与事件订阅，不提供玩法。

先阅读根目录 `MODULE_DEVELOPMENT.md` 和 EternalSDK 公共头。复制此目录到新业务域目录后，修改模块 ID、显示名、target 与描述符，按实际能力声明依赖。默认优先在已有模块内增加功能，只有独立业务域才增加 DLL。

配置文件部署到 `Eternal/config/example/example.json`，其目录来自 HostContext。代码不读 Host 私有配置、其他模块数据库或源码。可选 Core 服务缺失时示例仍能启用；停用后回调停止处理事件。

`tests/ModuleContract.cpp` 是独立测试起点。完整集成测试还需验证启停、事件注销、缺依赖和真实 BDS 适配。此示例不打入正式部署包。
