# 测试报告

更新：2026-10-04。PASS仅用于真实已执行结果；PLANNED、NOT RUN、BLOCKED与失败必须保留。

## 已验证的Phase1历史基线

| 检查 | 类型 | 结果 |
|---|---|---|
| BDS1.26.51.1/LL26.51.6基础启动与stop | 真实BDS | PASS |
| 最小EternalCore DLL/版本诊断 | 原生构建与真实BDS | PASS，完整重启回归通过 |
| SDK布局 | C11/C++20 Windows目标静态编译 | PASS；不代替动态调用 |
| SDK版本/结构/生命周期/unsupported | 独立C++执行 | PASS |
| 最小DLL导出/自检 | 原生DLL与真实BDS | PASS |
| Core事务域 | 独立SQLite/合成身份 | PASS，17组；严格编译 |

详细历史结果在docs/phase1-evidence.json。曾缺少SymbolProvider导致首命令崩溃，修复后两次实际启动/命令/停服通过。测试含C++异常注入和真实子进程_Exit恢复；不等同断电、硬件故障或原生玩家资产验收。

## Phase1.5验收门槛

| 检查 | 当前状态 | 证据要求 |
|---|---|---|
| 迁移布局后域实现与旧基线一致 | 已由本地回归验证 | 实现blob对照及17组执行 |
| HostMod/八模块入口/Example原生对象 | PASS，本地clang-cl | 仅编译，不代替最终DLL链接和真实加载 |
| Host与八模块正式构建 | 实施中 | 当前产物与输入/摘要/导出检查 |
| 模块版本/依赖/生命周期/错误回滚 | 待记录 | 独立Host测试与真实模块DLL |
| Host唯一LL插件/真实BDS | 待记录 | ll list、诊断、停用恢复、stop、重启 |
| C/C++SDK/ABI新目录验证 | 待记录 | 实际编译与动态调用 |
| CI与发布allowlist | 待记录 | 实际运行/包清单，不仅workflow文件 |
| staged/public树及祖先审查 | 待最终树 | Test-PublicTree与人工license/privacy检查 |
| 原生玩家资产/能力链 | NOT RUN | 可信入口、授权撤销与真实客户端 |
| 业务/旧数据迁移/视觉 | NOT RUN | 各阶段用例和客户端效果 |

Phase1.5结果由最终当前树重新执行后补录，不能引用旧EternalCore DLL摘要冒充新Host验收。

本轮公开资料检查：合成服主fixture重跑17组通过；5个domain实现blob与本地Phase1基线相同；22份许可证文本/notice SHA验证及21份根/docs链接检查通过。公开扫描自测9组通过，但最终staged/public-main树仍须组装后再审。

## 后续必测

身份唯一/改名、OP与职司分离、打开UI后撤权、伪造/过期能力、金额/UTF-8边界、同作用域幂等、并发/busy、溢出、账本对账、故障前后恢复、outbox乱序、交付不确定、土地相交/保护、负载与真实客户端体验。Mock、独立测试、真实BDS和客户端结果分别记录。
