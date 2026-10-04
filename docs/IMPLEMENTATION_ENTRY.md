# 开发入口

先读根目录CURRENT_STATUS、ARCHITECTURE、API/ABI、DATABASE、BUILD、TODO和TEST_REPORT，再读REQUIREMENTS。Phase1历史基线已经验证；当前Phase1.5将运行图改为Host与八内部模块，需要新证据。

目录：host/EternalHost 为LL适配；modules/EternalCore/domain 为内部事务域；modules/EternalCore/api 为服务实现；sdk/EternalSDK 为公共契约与辅助头，sdk/EternalSDK/include 为标准include转发路径；migrations/EternalCore 为唯一编号SQL。业务模块不包含私有域，也不打开他人数据库。

独立域17组通过但没有原生玩家资产入口。能力链、正式拒绝回执、投影和物品恢复未完成；SDK中身份示例/描述符不能当作已注册引擎事件。

server、.deps、artifacts为本地运行/构建数据，不公开。公开文档只保留脱敏验收摘要。部署先停服，不能热覆盖DLL；模块卸载只有真正完成回调排空/服务撤销后才开放。
