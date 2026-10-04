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
| Host与八内部DLL本地构建 | PASS，九个DLL完成编译/链接 | 本地clang-cl/MSVC ABI回归；终止交接新版Host已重编并完成真实复验 |
| 七个console测试 | PASS，本地真实执行 | CoreDomain、CoreApi、HostRuntime、ExampleContracts、ModuleArtifact、Config、SDKCpp |
| 模块版本/依赖/生命周期/错误回滚 | PASS，本地mock及真实模块DLL | 初始18组Host，首次交接增至20组，终止交接增至23组mock通过；新Host实际停服清理通过 |
| Host唯一LL插件/真实BDS | PASS，新Host两次完整运行 | 启动/诊断/手动停用恢复/重启通过；两次stop均明确cleanup PASS、无关闭错误、退出码0，无玩家 |
| Core/Module两套SDK契约 | PASS，本地C11/C++20布局及C++辅助验证 | 静态布局与console验证，不代替引擎动态验收 |
| 云CI与发布allowlist | 首次实际CI失败；本地修复已准备，未通过 | libhat依赖安装/下载失败，尚未进入业务源码编译；修复尚未推送重跑 |
| staged/public树及祖先审查 | PASS，首次156文件源码树 | 后续改动须重新暂存审查；默认提交邮箱公开已获用户授权 |
| 原生玩家资产/能力链 | NOT RUN | 可信入口、授权撤销与真实客户端 |
| 业务/旧数据迁移/视觉 | NOT RUN | 各阶段用例和客户端效果 |

Phase1.5结果按实际产物补录，不能引用旧EternalCore DLL摘要冒充新Host验收。当前PASS覆盖本地构建、console/SDK验证、23组Host mock及指定新Host产物的完整真实BDS生命周期；云CI仍无通过结果，阶段不推进至Phase2。

本地七个console的执行日志与九DLL构建receipt保留在artifacts，未将带私人路径的原日志公开。Host mock已从20组增加至23组，包含首次启动线程至服务器线程交接的限制，以及服务器线程仍存活时拒绝终止交接、线程实际退出后的逆序最终清理与永久禁止重新启用、缺少OS退出证明时拒绝和清理失败隔离。23组可执行测试已额外实跑通过；此处通过不代表公开typed引擎钩子或真实BDS停服验收。

本轮公开资料检查：合成服主fixture重跑17组通过；5个domain实现blob与本地Phase1基线相同；22份许可证文本/notice SHA与Git过滤字节验证、35份Markdown的34个本地目标检查通过。公开扫描自测9组、156文件暂存扫描通过；首次源码公开commit父链仅含无父的初始README，私人基线未进入该公开父链。

后续补查官方LL的libhat传递构建依赖，完整MIT文本与固定commit/归档摘要加入通知，许可证清单增至23项，原22项保持不变；新增文件须纳入下次暂存与公开树复核。

真实BDS首轮manifest将加载器误声明为插件依赖而被拒，修正后Core加载成功但Host因Load/Enable线程不同拒绝Enable。增加显式首次线程交接并重编后，BDS1.26.51.1/LL26.51.6的两次启动检查通过：plugins仅LL与Eternal，LL列表仅一个Eternal mod；Core内部服务features=0、健康检查通过；首次手动disable/enable/selfcheck通过；第二次完整重启后的检查通过，测试无玩家，服务器已停止。

前一次验收的两次stop进程退出码均为0，但rawlog复查发现LL关闭阶段在另一线程调用HostMod.disable，出现Wrong Host thread和停用失败。因此撤回当时仅凭退出码得出的完整生命周期PASS，旧产物停服阶段未通过。该失败记录保留，之后修复并重新执行真实验收。

终止修复：Runtime保留Windows SYNCHRONIZE句柄，finishStopAfterServerThreadExit只在WaitForSingleObject(..., 0)明确signaled后最终交接、逆序Disable/Unload，永久禁止Load/Enable；普通Disable仍保留线程检查。薄适配层在GamingStatus::Stopping且非绑定线程时只延后请求，typed leaveGameSync钩子在origin返回后才尝试实际完成，并以OS句柄核验线程退出。23组mock通过；SDK只补契约注释，布局不变。

新Host在BDS1.26.51.1/LL26.51.6完成两次完整运行：启动、手动disable/enable、Core自检和完整重启检查通过；两次stop均明确记录“EternalHost stop cleanup PASS; internal modules disabled and unloaded”，未出现关闭Eternal错误或Wrong Host thread，退出码均为0。smoke现在同时要求终止清理PASS和不存在关闭错误，不再仅凭退出码判定。无玩家，服务器已停止，features=0，业务接口仍关闭。

旧部分验收Host SHA-256为72bd3c5b36d45163d3cc2ac2e378f1bfc25a1959945aa8736aac88869453f216；本次完整验收Host SHA-256为4a4bef31d753fb07918b4d0fee4e7a4ea6475cb3e14d012e0844e4e6ccade9db，已复核本地DLL一致。选中诊断及结构化证据在docs/phase1.5-evidence.json。新结果仍不证明玩家资产、玩法或客户端体验，且不替代云CI。

云CI首次运行37193348084在官方libhat包安装/下载阶段失败，不是业务源码编译错误；本地下载修复已采用固定摘要的官方归档来源，尚未推送重跑。保留首次失败事实，修复后须重新执行，未通过前不推进Phase2。

## 后续必测

身份唯一/改名、OP与职司分离、打开UI后撤权、伪造/过期能力、金额/UTF-8边界、同作用域幂等、并发/busy、溢出、账本对账、故障前后恢复、outbox乱序、交付不确定、土地相交/保护、负载与真实客户端体验。Mock、独立测试、真实BDS和客户端结果分别记录。
