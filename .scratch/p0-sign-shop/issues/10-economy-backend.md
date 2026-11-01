# 10: 经济后端

**What to build:** 按 `auto` 顺序探测经济后端：LegacyMoney 优先，其次记分板，最后空实现。LegacyMoney 下每笔交易走一次原子转账；离线店主按后端能力决定能否交易；商店税与 LegacyMoney 的 `pay_tax` 冲突要显式警告。

**Blocked by:** 04

**Status:** resolved

- [x] 服务器装了 LegacyMoney → 自动使用它，交易能在其数据库中看到
- [x] 没装 LegacyMoney → 回落到记分板，余额能正常增减
- [x] 记分板目标名可配置，缺失时按配置决定是否创建
- [x] 两家都没有 → 明确提示"没有可用经济后端"，交易按钮禁用
- [x] 店主离线且后端不支持离线 → 买与卖都被拒绝并说明原因
- [x] 店主离线且后端支持离线（LegacyMoney）→ 交易正常完成
- [x] 商店税开启时：店主实收 `总额 - 税`，税被销毁；启动时打出要求关闭 `pay_tax` 的警告
- [x] 余额写到 32 位后端时不会静默截断

> 勾选口径与 07/08/09 一致：勾上表示**代码已落地、有单测或文档支撑**；BDS 与 LegacyMoney 的
> 真实行为只能真机确认，逐条列在 Answer 末尾的「只能真机验收的项（已转 12）」，
> 对应 12 的 `### 从 10 转来的待验项`。

## Comments

- **已随 04 落地的那一半**：记分板后端本身、`auto` 落到「目标存在就用记分板」、店主离线时买方向拒绝并提前藏按钮、`economy.backend`/`scoreboardObjective`/`currencySymbol` 三个配置键。本票剩下的活是 LegacyMoney 探测、`createObjectiveIfMissing`、`requireOwnerOnline`、商店税，以及把 `auto` 补成 §8.4 的完整顺序。

## Answer

**已实现（`xmake build Shop` / `ShopTests` 通过，`xmake test` 199 cases / 2888 assertions 全绿）**

- `src/shop/Economy.{h,cpp}`（纯逻辑，不含 `mc/`）：
  - `selectBackend(configured, candidates)`：DESIGN §8.4 的候选顺序**只在这一处定义**
    （`auto` 与显式 `legacy_money` 都是 `legacy_money → scoreboard → null`，`scoreboard` 是
    `scoreboard → null`，`null` / `builtin` 直接是空实现）。
  - `probesFor(configured)`：要探哪几个候选也只由它回答，mod 层不再自己写一遍 `auto || legacy_money`。
  - `backendSelectionText` / `probeIssueText`：日志文案都在这一层，mod 只负责把事实填进去。
  - `ownerUnreachable(ownerOnline, backendSupportsOffline, requireOwnerOnline)`：离线判据的纯函数，
    点牌藏按钮 / `prepareSell` / `planTrade` 三处共用。
  - `checkTransfer(...)`：金额、空付款方、自转自、系统账户这四条校验与文案只有一份，默认实现与后端实现共用。
  - `EconomyProvider::supportsMoneySink()`（默认 `false`）：能不能把钱付给「系统」（空账户键 = 销毁）。
    不声明支持的后端收到空收款方会直接拒绝，不会被当普通账户入账。
- `src/shop/TradePricing.{h,cpp}`：`shopTax(total, rate)`（向下取整、恒落在 `[0, total]`，NaN/负税率/非正总额都是 0）
  与 `affordableUnitsWithTax(unitPrice, funds, rate)`（含税份数上限，二分到精确值）。
- `src/shop/TradePlan.{h,cpp}`：`TradeRequest` 新增 `requireOwnerOnline` / `taxRate` / `taxOnPayer`，
  `TradePlan` 新增 `tax`；`maxTradeUnits` 增一个「已算好可承受份数」的重载，`planTrade` 只算一次。
- `src/mod/LegacyMoneyEconomy.{h,cpp}`（新）：`GetModuleHandleW(L"LegacyMoney.dll")` + `GetProcAddress`
  解析五个未修饰 C 导出；五个操作全部实现，`supportsOffline()` 与 `supportsMoneySink()` 都是 true。
  `transfer` **只走一次** `LLMoney_Trans`（不自己拼 `Reduce` + `Add`），`to` 为空 = 销毁。
- `src/mod/ScoreboardEconomy.*`：`create(name, createIfMissing, issue)`；目标缺失且开关打开时用引擎自己的
  `dummy` 判据建（`Scoreboard::getCriteria("dummy")` + `addObjective`，拿不到判据就判 `ObjectiveCreateFailed`）。
- `src/mod/EconomyService.*`：完整探测 + 降级 + 日志；`ServerStartedEvent` 在世界就绪后立刻探一次
  （结果进启动日志），首次动钱时兜底；结论（含「一个后端都没有」）缓存；`initEconomyService` 打
  `pay_tax` WARN，后端是记分板而税率 > 0 时另打一条「商店税在此后端不生效」。
- `src/mod/Config.*`：`economy.createObjectiveIfMissing`、`economy.requireOwnerOnline`、`tax.shopTaxRate`
  （校验 `[0, 1)`）、`tax.taxDestination`（占位，P0 不生效）；`legacy_money` 不再是配置错误，`builtin` 仍拒绝
  （错误提示只列真正可写的四个值）。
- `src/mod/TradeService.cpp`：`Payment` 拆成 `main` / `tax`；`moveMoney` 只移动 `main`，`chargeTax` 收税，
  `settleTax` 是买卖共用的收税收尾。买卖两侧都在物品成功之后收税，失败则物品 + 主交易一起回滚。
- `README.md`：补「经济后端」「商店税（含 `pay_tax = 0` 的硬性前提）」「P0 已生效的配置项」三节；
  顶部「功能未实现」的陈旧状态改成现状。
- 测试：`EconomyTest`（+8 例：探测顺序、显式后端降级、`builtin` 不收口、探测计划、文案、离线判据矩阵、
  空账户转账、`checkTransfer`）、`TradePricingTest`（+3 例：税额舍入与边界、NaN、含税份数）、
  `TradePlanTest`（+8 例：`requireOwnerOnline` 两向、`tradeTaxPlan` 的方向×能力矩阵、`payerTaxRate`、
  买入税额、税压在谁身上两种、卖出含税上限）。
  注：`prepareCommon` 在 mod 层、`ShopTests` 测不到，所以「谁往 request 里填税率」这件事靠 `tradeTaxPlan` 的
  纯函数用例钉住，而不是靠 `planTrade` 的用例。

**决策与偏离**

- **收税排在物品那一步之后**（偏离 §8.9 字面的「主交易后紧接着收税」）。理由：税一旦销毁就补不回来——
  若先收税，之后移物品失败时退款只能退 `总额 - 税`，买家凭空少一笔税，§10.2 的「任一步失败两边余额不变」
  当场不成立。挪到最后，任何失败点都只涉及已经移动过的金额（买入 `T - X`、卖出 `T`），补偿永远精确；
  代价只是「物品已成交、税还没收」那一小段崩服会丢税，不会丢玩家的钱。已改写 DESIGN §8.9 与 §10.2 的原则段。
- **卖出方向店主多付一笔税**：`Trans(owner, seller, T)` + `Trans(owner, "", X)`，玩家拿到的仍是牌面 `T`，
  店主总共付出 `T + X`——这才是 §8.9 表格里「店主承担」在卖出方向上的含义。因此卖出的份数上限按含税成本算。
- **「这笔生意要抽多少税」与「税要不要算进付款方的可承受量」是两个字段**（`taxRate` / `taxOnPayer`），
  而两者的取值由纯函数 `tradeTaxPlan(direction, rate, supportsMoneySink)` 决定。这是两轮评审各抓到一次的回归：
  第一版把两者混成一个字段，买入方向把税率填 0，结果一分税都不收；第二版两个方向都填税率，
  却把含税价喂给了买入方向的份数上限——钱够的买家被判「钱不够」，税率 10% 时点开份数表单甚至有 0 份的死表单。
  现在 `taxRate` 两个方向都传（税总要抽），`taxOnPayer` 只在卖出方向为 true，
  份数上限统一走 `payerTaxRate(request)`（买入恒为 0），这三个判断都收在 `src/shop/TradePlan.*` 并有单测。
- **含税份数用二分而不是「资金 ÷ (1 + 税率)」**：double 的 `1.1` 是 `1.10000000000000008…`，`110 / 1.1` 落成
  `99.999…`，截断后会少算一份（店主明明付得起 10 份）。二分用的谓词就是真正会收的 `shopTax`。
- **显式 `legacy_money` 探测不到也降级**（而不是拒绝加载）：§8.4 第 4 条明确「加载失败不 panic：降级到下一个候选」。
  折中是降级 + 把原因原样打进日志。
- **`LLMoney_Get` 的 `-1` 映射成「读不到」**：读 LegacyMoney 的 `src/API.cpp` 确认空 XUID 与数据库异常都返回
  `-1`（不是 0），所以这个后端**有**失败通道；票面事实清单第 5 条担心的「未知账号读成 0」实际是
  「插入一行 `def_money` 再返回默认金额」，属于它的账户语义，已写进 DESIGN §14。
- **`val == 0` 必须自己跳过**：源码里 `LLMoney_Trans` 对 `val == 0` 是合法的，会白写一条 0 元流水。
- **记分板不收商店税，而不是「少一条流水」**：那个后端没有能把钱移出经济系统的账户，硬算下去只会让
  店主少收、买家少付，而没有任何地方记着这笔税。所以 `supportsMoneySink()` 为 false 时调用方直接把税率置 0，
  并在启动日志里写明「商店税在这个后端上不生效」。这一条在评审里被指出前一版描述是错的，已改代码与文档。
- **`/shop economy` 状态输出没做**：P0 没有命令基础设施（§12 把 `/shop` 管理命令列在 P1），所以「最终选择」
  落在启动日志与首次动钱时的日志里。DESIGN §8.4 第 4 条已同步成「写进日志（状态命令留 P1）」。
- **不缓存 `LLMoney_Get`**：动钱前的余额必须新鲜（`accountsRecordable` 与 `planTrade` 都读它），TTL 缓存会把
  「余额不足」判错。写副作用（插入 `def_money`）对同一个 XUID 只发生一次。
- **`tax.taxDestination` 只占位不生效**：P1 才做税收账户，已在 DESIGN §15 点名。

**已知边界**

- **`get()` 在 LegacyMoney 上「读不到」只剩负值一种来源**：从未上账的 XUID 会被插入 `def_money` 再返回它，
  所以「新账户」读到的是默认金额（通常 0）。商店税与补偿都不依赖这条区分（动钱侧用 `transfer`）。
- **`available()` 不会重新确认 DLL 还在**：后端被选中后 DLL 被卸载时，`economyBackendAvailable()` 仍报 true，
  真正的失败点落在 `get`（返回空）与 `transfer`（拒绝 + 一条 error）。`get`/`transfer` 里各做一次
  `GetModuleHandleW` 检查，避免调用悬空函数指针；检查与调用之间仍有理论窗口。
- **`pay_tax > 0` 时补偿金额会漂移**：反向 `Trans` 到账的是 `val - val*pay_tax`。P0 只能启动 WARN，
  **不**自动禁用商店税（读不到就不猜）。
- **导出名只认未修饰的 C 名**：拿不到就判 `ExportsMissing` 并降级，不去猜 C++ 命名空间版的 mangled 名。
- **`builtin` 仍是配置错误**：P1 才做自建账本；写它会拒绝加载，而不是悄悄用 auto。
- **探测结论会缓存**：装好 LegacyMoney 或建好记分板目标后需要重载模组（`/shop` 关开一次）才会重探。
- **`requireOwnerOnline = false` 不等于记分板支持离线**：只是不再提前拦，提交时仍会因「读不到店主的余额」失败。
- **`ObjectiveCriteria` 不自己造**：只通过 `Scoreboard::getCriteria("dummy")` 拿引擎注册的那一份。
- **评审建议未采纳的两条**：① `ScoreboardEconomy` 把 `ScoreboardId const&` 的地址存进 `ScoreTarget`
  （沿用 04 的既有写法，依赖引擎按引用返回，属本票之前的问题，改它要连 `resolveTarget` 一起动，留给 12 的真机结论）；
  ② `LegacyMoneyEconomy` 没覆写 `available()`（理由见上一条）。

**指标**

- `xmake build Shop`：`Shop.dll` 构建通过（clang-cl，`/W4` 无新增告警）；`llvm-objdump -p` 确认导入表里
  **没有** `LegacyMoney.dll`、**没有** `LLMoney_*` 导入项（「可选后端」的硬前提）。
- `xmake build ShopTests` + `xmake test`：199 cases / 2888 assertions 全绿（10 之前 180 / 2761）。
- `git diff --check`：无空白问题。

**只能真机验收的项（已转 12）**

1. 装了 LegacyMoney 时 `auto` 真的选中它，交易在 `economy.db` 的 `mtrans` 里能看到。
2. 没装 LegacyMoney 时回落到记分板，余额正常增减。
3. `createObjectiveIfMissing` 真能建出 `money` 目标；关掉时明确拒绝。
4. 两家都没有时表单写「没有可用的经济后端」且无买卖按钮。
5. 店主离线 + 记分板：买卖都被拒且原因正确；`requireOwnerOnline = false` 时不再提前拦但依旧成交不了。
6. 店主离线 + LegacyMoney：买卖都正常完成。
7. 商店税：店主实收 `总额 - 税`、税被销毁（两条 `mtrans`）、启动 WARN 出现；卖出方向店主付 `T + X`。
8. 税为 0 时只有一条流水；记分板后端 + 税率 > 0 时税不生效且日志有说明。
9. 32 位记分板后端写超范围时被拒而不是截断。
10. `pay_tax` 的实际扣法（0.05 时收款方少收）与文档一致。

## 后续修正（2026-11-01）：删掉 `auto`，不做任何自动降级

**决定**：`economy.backend` 只允许 `legacy_money` / `scoreboard` / `null`，**默认 `legacy_money`**；
`auto` 从枚举、名字表、探测计划、选择逻辑里整体删除。老配置写 `auto` 会被配置校验拒绝加载，
并给出专门的迁移提示（不是泛泛的「只能是 A / B / C」）。同时 `economy.createObjectiveIfMissing`
的默认值从 `false` 改为 `true`。

**为什么**：本票原设计里「显式 `legacy_money` 探不到也降级」（当时的理由是 §8.4 第 4 条
「加载失败不 panic」）配合默认值 `auto`，等于让账本可以**在服主不知情的时候换一套**：LegacyMoney
因版本不兼容导致导出缺失时，本模组会改用记分板记账，玩家余额看起来一夜之间全变 0，而日志里
只有一条容易被忽略的 info。这与本工程「配置非法就拒绝加载、不带着半份配置跑」的既有原则直接冲突。

**落地**

- `shop/Economy.h`：`Backend` 去掉 `Auto`；`probesFor` 只探配置写着的那一个；
  `selectBackend` 删掉跨后端分支（探不到就是 `Null` + `skipped` 原因）。
- `mod/Config.h`：默认 `backend = "legacy_money"`、`createObjectiveIfMissing = true`。
- `mod/Config.cpp`：`auto` 单列一条迁移错误；`builtin` 的提示文案里去掉 `auto`。
- `mod/EconomyService.cpp`：`noBackendHint` 认识「你写的是哪个」，按 `ModuleMissing` /
  `ExportsMissing` / `ObjectiveMissing` / `ObjectiveCreateFailed` 给不同建议，并明写
  「本模组不会自动改用另一个后端（账本换家必须由你在 `economy.backend` 里决定）」。
  记分板探测不再需要 `legacyMoney` 的探测结果。
- 测试：删掉 4 个 `Auto` 用例，改为「写了哪个后端就只认哪个：探不到也不许换成另一个账本」
  （正向 + 反向各一条），并新增 `parseBackend("auto")` 必须为空、提示文本里不许再出现 `auto`。
- 文档：`DESIGN §8.4`（含两档后端差异对照表）、§15 默认值、§14 风险表（本项标「已移除」，
  管理员店系统账户改「已真机验证」，新增「`LLMoney_Add` 对未上账 XUID」已由源码证伪）、
  `README` 的「经济后端」整节、`CHANGELOG`「变更」。

**顺带核实到的 LegacyMoney 源码事实**（`LiteLDev/LegacyMoney` 的 `src/API.cpp`）：

- `LLMoney_Get`：查不到行时**先 `insert` 一行 `def_money` 再返回**；只有空 XUID 或数据库异常返回 `-1`。
- `LLMoney_Add(xuid, n)`：走 `Trans("", xuid, n)`，**从未上账的 XUID 也能加钱**（会顺带建行）。
- `LLMoney_Trans`：`n < 0` 或 `from == to` 直接 false；`from` 空 = 凭空记，`to` 空 = 销毁
  （仍写一条 `mtrans`，界面显示成 `System`）。
- `pay_tax` 只在 `from` 非空时对**收款方**扣 `val × pay_tax`，而 `mtrans` 记的是**税前** `val`
  → 所以「开商店税就把 `pay_tax` 设为 0」是必需而不是保守。

**指标**：`xmake build Shop` 通过（无新增告警）；`xmake test` 207 cases / 2944 assertions 全绿。
