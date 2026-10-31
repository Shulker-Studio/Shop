# 更新日志

本项目所有值得记录的变更都会写在这里。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 基于 LeviLamina 模组模板初始化工程：target `Shop`，命名空间 `ss::shop`，类 `Shop`。
- 依赖 LeviLamina `26.51.8` 与 iListenAttentively `0.17.0`（事件层）。
- `DESIGN.md`：完整设计文档（数据模型、事件与 hook、交易流程、商店箱保护、经济契约、决策记录）。
- `README.md`：中文项目说明与构建/安装指引。
- `tooth.json`：填充仓库地址、运行时依赖与发布产物路径。
- 告示牌四行解析与规范化（`src/shop/ShopSign.*`）、份数计价（`src/shop/TradePricing.*`），以及 doctest 测试设施（`xmake test`）。
- 开店流程：监听告示牌正面编辑，校验并规范化四行，失败只提示不删牌（`src/mod/ShopService.*`、`SignEditListener.*`）。
- 绑定规则：牌子必须附着在容器上（墙牌看背面、站立看下方、吊牌看上方），双箱合并为 54 格，一个容器只被一块牌绑定（`src/shop/ContainerFinder.*`、`src/mod/ContainerFinderBds.*`）。
- `config.json` 加载与校验：`adminShopName`、`shopContainers`、`maxShopAmount`、`blockSellPriceHigherThanBuy`、`shopInteractionCooldownMs`；配置非法时拒绝启用。
- 点牌看店：`PlayerInteractBlockEvent` 只拦"正面是合法商店牌 + 附着容器可用"的牌子（06 起判据改为「正面像商店牌 + 记录的店主」，见下文），弹 `SimpleForm` 展示店主、商品、每份件数、买入价、卖出价、可成交份数与余额（`src/mod/SignInteractListener.*`、`ShopForm.*`）。
- 店主判定：读正面 `mLastEditXuid` 经 `PlayerInfo` 反查当前名（`src/mod/ShopOwner.*`）；编辑落地后在 `PlayerEditSignAfterEvent` 里按归属规则写一次 ownerID（具体规则见下面「变更」里的 `recordShopOwner`），补上包路径不写 `mLastEditXuid` 的空洞。
- 可成交份数与商品统计：商品以第一个非空槽位为准，合并双箱统计严格同种（类型 + aux + 用户数据）的总件数（`src/mod/ContainerFinderBds.*`）。
- 表单文案、连点冷却与可成交份数都是纯逻辑（`src/shop/ShopInfo.*`、`TradeGate.*`、`TradePricing::unitsFromItems`），有 doctest 覆盖。
- 经济后端契约与空实现（`src/shop/Economy.*`）：金额一律 `int64`，`transfer` 失败保证双方余额不变，补偿失败返回 `Broken`；`NullEconomy` 任一账户的余额都读不到（不是「读 0」）、不算可用后端。
- 记分板经济后端（`src/mod/ScoreboardEconomy.*`）：余额 = 玩家在 `economy.scoreboardObjective` 上的分值，写入前过 `toScoreValue` 检查 `int32` 范围，超范围拒绝而不是截断；只支持在线玩家。
- 后端选择与账户查询（`src/mod/EconomyService.*`）：按 `economy.backend` 惰性选定（04 只有记分板与空实现，目标必须已存在），选不出可用后端时打错误日志并降级为「交易全部失败」。
- 买入交易（`src/shop/TradePlan.*`、`src/mod/TradeService.*`）：`planBuy` 纯逻辑算出成交份数、件数与总价（含部分成交与原因判定）；`buyUnits` 按「预校验 → 快照 → 先扣钱 → 先取货再给货」执行，任一步失败按快照回写物品并退回钱款；补偿失败打 `fatal` 并通知在线 OP。
- 份数表单（`src/shop/UnitsForm.*`、`src/mod/ShopForm.*`）：下拉框 `1 / 2 / 4 / 自定义份数`，正文逐档列出总件数与金额；上限 = `maxShopAmount`、货的份数、收货方容量、付款方余额取最小（DESIGN §10.1 第 3 步 / §10.3）。只能部分成交时先弹确认表单，把重算后的真实份数与金额摆出来，确认后才成交。
- 容器刷新与冻结：成交后对商店箱两半各 `BlockActor::refresh` 一次（DESIGN §7.3 / §10.2 第 9 步）；补偿失败时把「玩家 + 商店」钉进内存冻结名单，后续提交拒绝并通知在线 OP。
- 部分成交/改价的确认：实际份数或金额与份数表单上看到的不一致时，`buyUnits` 不动任何钱货，先弹确认表单，玩家点头才成交（DESIGN §10.3）。
- 表单「购买」按钮接上真交易：回调不碰世界，捕获 XUID 与牌子坐标后切到服务端线程重新解析玩家与商店（`ShopForm.*`、`TradeService::requestBuyForm`/`submitBuy`），并加 250ms 提交冷却。
- `config.json` 新增 `allowPartialTransactions` 与 `economy`（`backend` / `scoreboardObjective` / `currencySymbol`），以及 `ignoreCreativeMode`（创造模式禁止交易）；表单余额与货币符号改为读真实后端与配置。
- 卖出交易（`src/mod/TradeService.*`）：`sellUnits` 把货从玩家背包搬进商店箱、店主付钱给玩家；预校验、快照、确认表单、补偿与冻结全部复用买入那一套，只有「谁付钱、谁收货、钱进钱出哪个方向」反过来。卖出容量算的是商店箱还能装几份（双箱按合并顺序，实际放货以 `Container::addItem` 剩余的件数为真值），放不下就按快照回写并退款。
- 买卖共用一份份数算法（`src/shop/TradePlan.*` 的 `TradeRequest` / `planTrade`）：买入与卖出只差「货在哪、谁收货、谁付钱」，三个上限与部分成交开关、拒绝原因的优先级都只写一遍；拒绝原因按方向换说法（「商店的货不够」/「你手里没有这么多」、「你的钱不够」/「店主的钱不够」）。
- 经济后端能区分「读不到余额」与「余额为 0」（`EconomyProvider::get` 返回 `optional`）：卖出时不会再对着一个有钱的店主报「资金不足」；动钱之前先确认两边账户都能记账（`EconomyService::accountsRecordable`），读不到就整笔拒绝、两边余额未动。
- 牌面防护（`src/shop/SignProtection.*`、`SignInteractListener.*`、`ShopService::planSignEdit`）：染料、墨囊、荧光墨囊、蜜蜡、斧头这些会改牌子外观的物品，只有店主与 OP 放行（交回原版处理），其余人一律在交互阶段取消并提示；**看背面也一样**（上蜡改的是整块牌子）。店主/OP **潜行 + 右键**才进原版编辑界面（背面照旧人人都能写），上蜡后连店主也被拦下并提示先除蜡；提交阶段还有一道兜底（非店主、以及上了蜡的牌，正面编辑包直接丢弃）。上蜡不改变商店语义，交易照常（DESIGN §9.2 / §10.1 / §11）。
- 「谁是店主」收成一个纯函数 `shopActorOf`（`src/shop/SignProtection.*`），交互阶段的表单角色与提交阶段的编辑权限共用它，不再各写一份 XUID 比较；`ShopInteraction` 新增 `actor` / `waxed` / `facingFront`（身份、上蜡与朝向各只读一次，都与「能不能交易」解耦）。
- 归属交接（`SignOwnerHandoff`，纯逻辑在 `shop/SignProtection.h`）：`planSignEdit` 在 Before 阶段判出「改之前是不是商店 / 是不是管理员店 / 原 XUID」，「该不该保住原店主」只由 `keepsPreviousOwner` 判，第 1 行写谁与归属写谁共用它。
- 物品分类按形状兜底：白名单（16 染料 + 7 斧头 + 墨囊/荧光墨囊/蜜蜡）之外，`*_dye` 与 `*_axe`（排除镐）也认，免得新版本加颜色/材质时静默放行。
- 商店牌还在但店停摆时（附着格不是可用容器 / 牌面已不严格合法）：能进原版编辑界面的判据与提交阶段相同（店主与 OP 放行，店坏了要能修回来），其余人拦下并分别提示「商店箱不在了」或「写法现在不算有效商店」。
- 管理员商店的身份判定：`shopActorOf` 新增 `adminShop` 入参，管理员店只有现任 OP 算管理身份（牌上记的创建者撤权后不再有权限，DESIGN §11）。
- 归属交接改成 4 条小环：两个玩家同一 tick 各改各的牌时，单槽会把前一个人的交接顶掉（那条 After 就什么也不写，引擎没写 XUID 的牌子会静默变成无主牌）。
- `tests/shop/SignProtectionTest.cpp`：物品分类、身份与权限、交互决策（含背面、潜行、上蜡、斧头没蜡）、提交闸与文案，共 26 个中文用例。

### 变更

- `ShopOwner::ensureOwnerRecorded` 改名为 `recordShopOwner`：归属在牌子第一次成为商店时写给编辑者，之后改牌面不改归属（After 阶段按 Before 记下的归属写回）。否则 06 的「OP 可以代改别人的牌面」会顺带把店主换成 OP，把原店主锁在自己的店外。
- `TradePricing::stockUnits` 改名为 `unitsFromItems`：商店箱的货与背包容量共用同一个「件数 → 份数」算式。
- `src/shop/BuyForm.*` 改名为 `src/shop/UnitsForm.*`：份数表单与确认表单只有一份实现，正文与措辞按 `TradeDirection` 参数化。
- `TradePlan::planBuy` 并入 `TradePlan::planTrade`（`BuyRequest` / `SellRequest` 合并为 `TradeRequest`，角色由调用方装）：买卖不再各写一份份数算法。
- `ShopSnapshot` 拆成「只看世界」的 `readShop` 与「看视角 + 读余额 + 定角色」的 `inspectShop`，交易路径复用前者。
- `ContainerFinderBds` 导出 `AttachedContainer` / `mergedSlots` / `sameShopItem` / `firstProductStack`，「合并槽位的顺序与边界」与「严格同种比较」各只有一处定义。
- `xmake.lua`：移除 server/client 双目标，固定为服务端模组；版本号由 `modpacker` 显式指定为 `0.1.0`。
- `manifest.json`：移除未被替换的 `platform` 占位符。
- CI：`build.yml` / `release.yml` 只构建服务端产物。
