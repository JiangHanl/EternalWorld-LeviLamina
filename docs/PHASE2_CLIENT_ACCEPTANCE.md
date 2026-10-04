# Phase 2 真实客户端验收步骤

只在独立开发测试服执行，使用新测试数据库。不要导入旧真实玩家资产。服务器日志、身份和数据库保持私人；公开报告只记验证结果。生产资产仍关闭，以下资产命令需要运营者单独开启 developmentValidation。

## 服主账号

进入后用 Core 诊断确认实际认证身份和角色；姓名不是授权键。

```text
/ecore native identity self
/ecore native roles self
/ecore native permission self money.adjust
/ecore native balance self money
/ecore native balance self reputation
```

角色诊断应包含 `owner`（服主）；若没有，停止资产测试并核对已确认的稳定身份配置，不通过用户名临时授予 Owner。保留初始余额结果，后续对照实际交易。

下面示例使用整数分，100 分为一两；每个测试 key 保持不变，理由也保持不变。执行第二次相同命令时应返回原回执且不再次增加余额。

```text
/ecore native asset self money 10000 p2-money-grant test-grant
/ecore native asset self money 10000 p2-money-grant test-grant
/ecore native asset self money -3000 p2-money-debit test-debit
/ecore native asset self money -1000000 p2-money-insufficient test-insufficient
/ecore native asset self reputation 50 p2-reputation-grant test-grant
/ecore native asset self reputation -10 p2-reputation-debit test-debit
/ecore native receipt p2-money-grant
/ecore native receipt p2-money-insufficient
/ecore native balance self money
/ecore native balance self reputation
```

在新零余额测试库中，余额不足应拒绝且没有账本半提交。同一 key 修改数量或理由应 CONFLICT，原交易和原回执保持不变。以上是测试步骤示例，不是已执行的真实玩家结果。

可对自己的非 Owner 职司做授予/撤销测试；这不会修改原生 OP。重连和服务端完整重启后，身份、当前角色、余额和原回执应保持。检查 Core 的 selfcheck 及私有测试快照，确认 ledger/receipt/外键一致；只看聊天成功提示不足以验收。

## 开发验证表单

```text
/ecore native authority-test
```

“执行测试”会尝试增加一两，再次核验当前主体、会话、权限和账户版本。取消不交易。等待超过 60 秒后点击，应拒绝并保留高价值拒绝回执，钱包/账本不变。运营者也可在页面仍打开时从真正控制台停用/启用 Eternal，旧页面随后必须被拒绝。

## 公开 SDK 调用链

由运营者在此测试服临时安装单独云 Artifact 中的 `CoreValidationModule`，并只给它批准所需 Core 模块能力。它只包含 SDK 和标准库，不读取 Core 私有文件；七个业务模块仍关闭。以下是预定步骤，执行结果须另记：

```text
/ecore native invoke core-validation check eeeeeeeeeeeeeeeeeeeeeeeeeeeeee20
/ecore native invoke core-validation check eeeeeeeeeeeeeeeeeeeeeeeeeeeeee20
```

此验证模块的 key 使用非零、32 位小写十六进制；原生诊断命令的文字 key 格式不受此限制。回调从真实认证玩家 Invocation 申请限定票据，通过公共服务查询身份/角色/权限/资产，固定增加 100 分（1 两），查询持久回执和 Outbox。第二次相同 key 必须复用原回执。开发客户端明确 opt-in，但 Core 私人开关和真实票据仍必须同时满足；默认生产客户端保持 Unsupported。重启后再执行相同 key 不再次发放。验证结束停服后撤下此测试模块，正式 Eternal 包只包含 Host 和八个模块。

## 普通玩家与撤权

需要第二个实际认证账号。先验证 Player 默认无管理权限；再由服主授予 EconomyManager，确认对应操作允许；玩家打开验证页面后，服主撤销职司，旧页面点击必须 DENIED。重连权限保持撤销。原生 OP 的赋予与移除不能自动更改 Eternal 职司。

只有服主账号时，上述普通玩家、双人转账及“他人撤权旧页面”项目保留 NOT RUN。服主过期页面或模块停用测试不能替代这个场景；合成 Runtime 测试也不能写成真实客户端通过。
