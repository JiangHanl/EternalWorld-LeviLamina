# 公开前审计

日期：2026-10-04。审计范围为源码、可公开文档、许可证与Git公开树；不打开真实玩家数据库、不修改运行数据、不推送仓库。当前Phase1.5与最终public-main尚在组装，不能把本报告当作最终发布PASS。

## 已完成

- 当前根文档与规格摘要移除运营者XUID/名字、私人Windows目录和旧服路径；测试服主改为合成身份。
- 事务域实现不修改；原型未接入玩家资产、拒绝请求暂无正式回执等限制保留。
- README中英文切换、Host与八内部DLL边界、C/C++SDK与数据所有权完成文档整理，未虚构新DLL/CI/客户端结果。
- 扩大.gitignore排除私人数据、运行文件、数据库、日志、密钥、包和工具链；已跟踪文件仍必须通过索引审查。
- 自主实现MIT与独立NOTICE，保存22份实际上游全文/完整原头声明及SHA-256清单；仅将换行统一LF并保留先前摘要，未改许可内容。公开文本原字节与Git过滤后字节一致。
- Test-PublicTree的9组规则自测通过，历史baseline扫描正确拒绝私人内容与祖先；匹配值不输出。

## 首轮工作树阻塞

当时检查117个文件，仍发现SDK旧README与四个旧tools入口包含绝对Windows路径。对应文件所有者正在改为相对入口/归档。原staged索引仍为旧基线，因此检查按预期失败；最终结果必须针对重新准备的索引与实际public-main再跑，不能沿用此轮检查数量。

后续工作树复核检查131个文件：旧tools路径已清除，目前仅SDK旧README仍命中私人Windows路径，已交由SDK所有者处理。该结果仍是组装过程中的检查，不是最终索引或公开提交通过。

SDK冻结后重新检查156个工作树文件，扫描通过；真实运营者身份的精确词检查无匹配，私人元数据字段仅出现在扫描器规则定义中。35份Markdown中34个本地链接目标全部存在。22份许可证的LF字节、清单SHA-256与Git过滤后字节再次一致，五个事务域实现blob保持历史基线原值。结果允许准备暂存索引，但不代替最终索引、公开提交或运行验收。

```powershell
& '.\tools\Test-PublicTree.ps1' -SelfTest
& '.\tools\Test-PublicTree.ps1' -WorkingTree
& '.\tools\Test-PublicTree.ps1' -Staged
& '.\tools\Test-PublicTree.ps1' -Treeish public-main
```

工具只读Git/文件，不建立commit、改写历史、创建远端或push。它检查私有路径/数字身份候选、明显凭据、运行/二进制目录和本地基线祖先；不会证明所有秘密都已发现。合成uint64边界值明确允许，不代表允许真实运营账号。

## 许可证结论与限制

已核验：LL开源部分LGPL/GPL原文、模板CC0、fmt与各头依赖原许可、SQLite公版声明、SymbolProvider固定原文件公版头与mingw-w64免责声明。没有把自主MIT扩展到第三方库或引擎内容。

SymbolProvider取得包无根LICENSE，结论只依据已核验原文件，不自动覆盖未来新增代码。levibuildscript/prelink取得目录没有足够的根许可材料供本工程重新发布整套源/工具；它们保持外部构建依赖，不进公开包。LL闭源部分、BDS、原版资源、微软包、运行符号数据不入公开树。

未来新增复制实现或扩展包范围须重新检查。发布包必须包含NOTICE、第三方说明和实际必要的完整许可证，而非仅自主MIT。

## 最终发布门槛

1. 当前树、最终索引和public-main扫描通过；人工检查测试/示例中没有真实身份、路径、凭据。
2. public-main与本地私人基线没有共同祖先；保留本地历史/标签，但只推public-main到远端main，不推--all/--tags/--mirror。
3. 公开commit作者/邮箱、远端地址和权限经人工核对；本报告不公布其值。
4. 新Host/模块/SDK真实构建与测试、运行证据和CI结果按实际更新；旧Phase1证据不代替新架构。
5. 最终包allowlist与第三方通知核对；不含server/.deps/世界/数据库/原版资源/未知许可工具。

任何一项未完成时都不称“安全开源发布已完成”。本轮没有执行push或运行服务器。

## 连接器首次提交的作者信息

当前GitHub连接器的create_file与create_commit参数不提供author/committer覆盖，也不支持查询账户的私人邮箱设置。GitHub官方API在省略这些字段时使用认证主体资料：[Contents提交默认值](https://docs.github.com/en/rest/repos/contents?apiVersion=2022-11-28#create-or-update-file-contents)、[Git commit默认值](https://docs.github.com/en/rest/git/commits?apiVersion=2022-11-28#create-a-commit)。因此不能承诺首次提交匿名，或保证邮箱为noreply。

初次Contents写入会直接更新公开分支，返回SHA后才能用fetch_commit核对实际作者/提交者；之后改写或删除分支不能撤回已经传播的信息。首次写入前须确认公开身份设置或接受该元数据。后续Git Tree提交仅使用已审查的公开父提交，不引用任何私人基线commit；提交对象仍须检查作者信息，不能把“源码已脱敏”当作提交元数据已脱敏。
