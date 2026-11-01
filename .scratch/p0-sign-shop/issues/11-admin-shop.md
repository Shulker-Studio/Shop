# 11: 管理员商店

**What to build:** OP 创建的管理员商店：第 1 行是配置的管理员名，库存来自容器，任何玩家都能买卖。非 OP 写同一个名字时只是普通商店。

**Blocked by:** 04

**Status:** resolved

- [x] OP 建的管理员店 → 其他玩家能正常买卖
- [x] 管理员店的库存来自容器，库存耗尽后拒绝卖出
- [x] 非 OP 在牌上写管理员名 → 创建为普通商店，店主是自己
- [x] 管理员店同样需要容器，没有容器时拒绝创建
- [x] 非店主仍不能打开管理员店的容器
- [x] 管理员店的牌面防护与普通商店一致

## Answer

**已实现（`xmake build Shop` / `ShopTests` 通过，`xmake test` 205 cases / 2929 assertions 全绿）**

- 真正的缺口只有一处：`TradeService::prepareCommon` 对管理员店整笔拒绝（「管理员商店的交易还没实现」，
  04/05 把资金端留给了本票）。现在管理员店与普通店走**同一条**交易路径，差异全部收在纯逻辑里：
  - `economy::shopSideAccount(adminShop, ownerXuid)`：店那一侧的账户。普通店 = 店主 XUID；
    管理员店 = 系统账户（空账户键），不拿牌上记录的创建者冒充收款方。
  - `economy::shopSideFunds(adminShop, ownerFunds)`：管理员店返回 `kUnlimitedFunds`（资金侧不设上限），
    卖出方向的份数上限只剩「你手里的货 / 商店箱还能装几份」。
  - `tradeTaxPlan(direction, rate, supportsMoneySink, adminShop)`：管理员店两个方向都不收商店税。
- `EconomyProvider::supportsSystemAccount()`（默认 `false`）：能不能把系统账户当交易对手。
  声明了就必须走通两个方向（买入失败要把钱从系统退回玩家，卖出失败要把钱从玩家退给系统），
  否则补偿路径会断。
- `checkTransfer` 改成向后端对象问能力（`checkTransfer(provider, from, to, amount)`）：空付款方 = 从系统付款
  （凭空记账，只有 `supportsSystemAccount()` 放行）、空收款方 = 销毁（`supportsMoneySink()` 或
  `supportsSystemAccount()` 任一）。新增「系统账户不能转给系统账户」（两边都空什么都不会发生）。
- 基类 `EconomyProvider::transfer` 补上两个系统账户方向：`from` 空 → `add(to)`，`to` 空 → `reduce(from)`。
- 后端：
  - `LegacyMoneyEconomy` 声明该能力，`from` 为空时走 `LLMoney_Add`（`LLMoney_Trans` 从空 XUID 扣款只会失败）；
    `to` 为空仍是 `LLMoney_Trans(to="")`。
  - `ScoreboardEconomy` 声明该能力，靠基类的 `add` / `reduce`（只能动在线玩家，而交易的那一头本来就在线）。
  - 两者都**没有**动 `supportsMoneySink()`：那一项只关系商店税，10 的口径（记分板整笔不算）不变。
- `src/mod/TradeService.cpp`：
  - `paymentFor(direction, info, ownerXuid, traderXuid, plan)`：一次交易的付款信息只拼一份，
    买卖方向只在这一处分叉（店那一侧走 `shopSideAccount`）。
  - `prepareCommon`：管理员店跳过「读不到店主」「不能跟自己交易」；后端声明不了系统账户 →
    `noSystemAccountReason()`（与点牌时藏按钮同一句）；`ownerOnline` 对管理员店恒为 true，
    `planTrade` 的离线判据不会误伤（§8.5 表格的「同左」）。
  - `prepareSell`：管理员店不读店主余额，`payerFunds` 用 `shopSideFunds`；`ownerBalance` 留空（不编数字）。
  - `buyUnits` / `sellUnits`：`Payment` 的店那一侧用 `shopSideAccount`；`moveMoney` 对空账户跳过
    「账户可记账」检查（能力已在预校验里判过），失败文案分「付款」「店主付款」「系统账户付款」；
    管理员店的部分成交提示不再提「店主的钱」。
- `ShopService::inspectShop`：角色改用 `shopFormRoleOf(actor, adminShop)`；后端声明不了系统账户时把原因写进
  `unavailableReason`（买卖按钮一个不给）。
- `ShopInfo::shopFormRoleOf(ShopActor, bool adminShop)`（新，与 `shopFormButtons` 同处）：普通店照旧
  「店主与 OP 拿管理入口」，管理员店**一律访客**。管理能力仍由 `shopActorOf` / `canManageShop` 判，一行没改。
- `ShopInfo::noSystemAccountReason()`（新）；`UnitsForm` 的卖出上限说明在管理员店不再写「店主余额」。
- 票面 6 条勾选项的现状（本票只补第 1 条，其余是核对）：
  1. OP 建的管理员店 → 其他玩家能正常买卖：**本票补的就是这一条**。
  2. 库存来自容器、库存耗尽后拒绝卖出：一直如此——商品权威是容器第一个非空槽位，`goodsUnits` 为 0 时
     `planTrade` 报缺货，管理员店没有「无限库存」分支。
  3. 非 OP 写管理员名 → 普通店、店主是自己：01/02 已落地（`isAdminShop` 要求 `editorIsOp`，
     规范化时第 1 行被写成编辑者名字），本票未改。
  4. 管理员店同样需要容器：`planSignEdit` / `recordShopOwner` / `readShop` 都不看是不是管理员店，
     没有容器就拒绝创建、也不算商店。
  5. 非店主不能打开管理员店的容器：07 的 `operatorOnly`（`adminShop || NoRecordedOwner`）与文案
     「这是管理员商店的箱子，只有 OP 能开」，本票未改。
  6. 管理员店的牌面防护与普通店一致：06 的 `shopActorOf`（管理员店只有现任 OP 算管理身份）与
     `signTextEditRejection`（上蜡对所有人拦），本票未改。

**决策与偏离**

- **资金端 = 系统账户生/灭钱（DESIGN 新增 D30）**：§7.4 原文写「服务器账户随 P1 落地」，而票面要求 P0 能买卖，
  两者冲突已由用户裁定取「生/灭钱」：买入销毁、卖出凭空记账。好处是两个方向开箱即用、LegacyMoney 与记分板
  都能落地；代价是卖出方向成了一个受「箱子空间」约束的印钞口。P1 的「系统账户作为真实账户」（余额可查、可充、
  `taxDestination`）没有动。
- **两项能力分开，不合并**：`supportsSystemAccount()`（管理员店）与 `supportsMoneySink()`（商店税）是两件事。
  合并会让记分板突然开始收税——那是 10 的口径（§8.9：整笔不算），不在本票范围内。
- **管理员店不收商店税**：税是「店主承担的那一份」，管理员店没有店主收入这一侧；再抽一道只是把买家的钱多烧一笔。
- **OP 也能买卖（D31）**：`shopFormRoleOf` 对管理员店一律访客，「不能跟自己交易」也不适用——对手方是系统账户，
  店主不是交易的一侧。管理能力（改牌、开箱、拆箱）仍只看 `canManageShop`，与表单按钮解耦。
- **`checkTransfer` 换成问后端对象**：能力来源从两个布尔参数收成「后端自己答一次」，基类与 LegacyMoney 两个
  调用点不会各写一遍 `sink || systemAccount`。
- **`LLMoney_Add` 承担凭空记账**：`LLMoney_Trans` 从空 XUID 扣款只会失败（`LLMoney_Get` 的 `-1` 哨兵就是这么来的），
  所以卖出方向换成 `Add`。这一条只能在真机确认，退路见「已知边界」。
- **`ownerBalance` 对管理员店留空**：不编「无穷大」也不编 0；卖出表单因此不显示「店主余额」那一行，
  上限说明也换了一句（`capHintOf`）。

**已知边界**

- 只做了「能力声明」，没有做「系统账户余额可查/可充值」：P1 的真实服务器账户、税收账户（`taxDestination`）未动。
- 记分板下卖出是给在线玩家加分、买入是扣分。玩家分值若在 `INT32_MAX` 附近，`toScoreValue` 会拒绝写入，
  表现是「系统账户付不出这一笔」，不会溢出。
- LegacyMoney 的 `LLMoney_Add` 是否接受「从未上账的 XUID」没有验证（`Get` 会插一行 `def_money`，`Add` 的路径没看过源码）。
  若它拒绝，表现是卖出方向一律「LegacyMoney 没能把钱记到收款方账上」；退路是给系统账户预建一行（P1 的真实账户）。
- 管理员店的「管理商店」入口仍是 P1 的桩：OP 在管理员店里拿到的是买卖按钮，改牌/开箱走既有的潜行右键与权限判据。
- 外部工具把非 OP 的牌写成管理员名的历史边角：`readShopOwner` 按牌面文字判定，仍会当成管理员店（03 已记的边界，
  本票不改口径）。
- 票面第 2~6 条本票只做核对，没有改动它们的代码路径；对应用例仍是 01/06/07 那批。
- `shopSideFunds` 用 `kUnlimitedFunds`（`INT64_MAX`）：卖出方向的 `affordableUnits` 会算 `INT64_MAX / 单价`，再被 `maxShopAmount`
  与货量夹住，不会溢出。

**评审（双轴，Standards + Spec 两个子代理并行）**

- **Standards 硬违规：DESIGN §8.9 没跟着改**（照旧写「承担方=店主」与 `Trans(owner, …)`）→ 已补：
  §8.9 的规则表加一行「管理员店不收税」，§8.1 把两个能力（`supportsMoneySink` / `supportsSystemAccount`）
  分开说明「用途不同、故意不合并」。顺带修掉 §3 的错字（「不分上限」→「不设上限」）与 §7.4 里
  对 §8.5 旧表格措辞的引用（「服务器账户」→「系统账户」）。
- **术语漂移**：`checkTransfer` 的注释用「动系统账户」指 `supportsMoneySink`，与 GLOSSARY 新增的
  「系统账户」撞车 → 改成「能不能用系统账户」，并把该函数里两个方向各需要哪项能力写清。
  GLOSSARY 原本就有 `**系统账户**` 条目（定义是「被销毁或无人接收的钱的归处」），
  与本票新增的重复且互斥 → 合成一条：空账户键，付给它=销毁、由它付=凭空记账，管理员店用两个方向、
  商店税只用「付给它」那一半。
- **Duplicated Code（判断题，采纳）**：买入/卖出各拼一份 `Payment`（付款方、收款方、主金额、税）→
  收成 `paymentFor(direction, info, ownerXuid, traderXuid, plan)`，方向只在这一处分叉。
- **Data Clumps / Mysterious Name（判断题，采纳）**：`shopSideFunds` 的 `INT64_MAX` 哨兵改为具名常量
  `kUnlimitedFunds`（并说明为什么不用 `-1`）。
- **测试冗余（判断题，采纳）**：`ShopInfoTest` 新增的那条「管理员店给访客的按钮」其实没验到 adminShop
  （`shopFormButtons` 不读这个字段），与既有用例重复 → 换成「管理员店 + 后端当不了系统账户 → 正文写原因、
  按钮一个不给」，角色映射的用例移到 `ShopInfoTest`（函数也挪过去了）。
- **分层（判断题，采纳）**：`ShopFormRole` 在 `ShopInfo.h`、映射在 `SignProtection.h`，为此让
  `SignProtection.h` include 了 `ShopInfo.h` → 把 `shopFormRoleOf` 挪到 `ShopInfo.h/cpp`（与
  `shopFormButtons` 同处），`SignProtection.h` 不再依赖信息层。
- **Shotgun Surgery（判断题，部分反驳）**：管理员店的特例确实落在四处（表单角色、资金账户、税、文案），
  但它们是四个不同语义，各自都有纯函数（`shopFormRoleOf` / `shopSideAccount` / `shopSideFunds` /
  `tradeTaxPlan` / `capHintOf`），mod 层一处也没有自己判 `adminShop` 去改钱或改税。把这些压成
  「一个 adminShop 分支包打天下」反而会让「税为什么是 0」「余额为什么读不到」变回隐式知识。
- **Spec：死接口（采纳）**：`prepareCommon` 改成问 `provider.available()` 后，`economyBackendAvailable()`
  没有调用者 → 删掉。
- **Spec：`moveMoney` 的报错原因（采纳，是 bug）**：管理员店有一头是空账户时一律报「后端没有系统账户」，
  但那种情况下可读的只剩玩家那一侧，`get` 失败应该是「读不到你的余额」（§8.2）→ 改掉并补注释。
- **Spec：`to` 为空的放行条件（采纳，改文档+注释而非改代码）**：`supportsMoneySink()` 仍是商店税的
  唯一判据（`tradeTaxPlan` 只读它），`supportsSystemAccount()` 才决定管理员店的资金端；`checkTransfer`
  在「付给系统」这一边接受两项中的任一，因为「能吃销毁」本来就是系统账户能力的一半。§8.1 已按这个口径重写。

**指标**

- `xmake build Shop`：`Shop.dll` 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `xmake test`：205 cases / 2929 assertions 全绿（本票之前 199 / 2888；
  新增 6 个中文用例、41 个断言）。
- `git diff --check`：无空白问题（`core.autocrlf=true` 只会提示换行符归一，不是空白错误）。

**只能真机验收的项（已转 12）**

1. OP 建管理员店，普通玩家买入：背包加货、箱子减货、玩家余额变少，且没有任何人收到那笔钱（`Trans(to="")` 真的销毁）。
2. 同一家店卖出：玩家余额变多、箱子加货（`LLMoney_Add` 真的凭空记账）。
3. 管理员店的创建者被撤权后再点牌：拿到的是买卖按钮，改牌与开箱被拦（管理入口消失）。
4. 管理员店买光（箱子空 / 不足一份）后点牌：正文写「商店箱里没有货」、按钮消失，补货后恢复。
5. 管理员店 + 记分板后端：买卖各一次，分值增减正确；`tax.shopTaxRate > 0` 时管理员店仍然不收税。
6. `LLMoney_Add` 对从未上账的账户的行为（若拒绝：卖出方向报「没能把钱记到收款方账上」）。
