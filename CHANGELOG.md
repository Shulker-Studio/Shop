# 更新日志

本项目所有值得记录的变更都会写在这里。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- `/shop status` 与 `/shop debug on|off`（P0.1 观测包）：前者随时输出配置快照、经济后端
  「配置值 → 实际值」、10 条 hook 的「已尝试注册 + attach 返回码」、模组与 iListenAttentively 版本；
  后者打开后在五个**结果点**各打一行 `[shop/dbg]`（`sign_decision` / `protection_block` /
  `trade_result` / `content_block` / `economy_resolution`），供手工验收时对照测试单条目。
  两者都只观测、不改任何交易/保护/经济判断；`debug` 不持久化，模组关闭即归零。
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
- 牌面防护（`src/shop/SignProtection.*`、`SignInteractListener.*`、`ShopService::planSignEdit`）：染料、墨囊、荧光墨囊、蜜蜡这些会改牌子的物品，只有店主与 OP 放行（交回原版处理），其余人一律在交互阶段取消并提示；**看背面也一样**（上蜡改的是整块牌子）。店主/OP **潜行 + 右键**才进原版编辑界面（背面照旧人人都能写），上蜡后连店主也被拦下（蜡不可逆，只能拆牌重立）；提交阶段还有一道兜底（非店主、以及上了蜡的牌，正面编辑包直接丢弃）。上蜡不改变商店语义，交易照常（DESIGN §9.2 / §10.1 / §11）。
- 「谁是店主」收成一个纯函数 `shopActorOf`（`src/shop/SignProtection.*`），交互阶段的表单角色与提交阶段的编辑权限共用它，不再各写一份 XUID 比较；`ShopInteraction` 新增 `actor` / `waxed` / `facingFront`（身份、上蜡与朝向各只读一次，都与「能不能交易」解耦）。
- 归属交接（`SignOwnerHandoff`，纯逻辑在 `shop/SignProtection.h`）：`planSignEdit` 在 Before 阶段判出「改之前是不是商店 / 是不是管理员店 / 原 XUID」，「该不该保住原店主」只由 `keepsPreviousOwner` 判，第 1 行写谁与归属写谁共用它。
- 物品分类按形状兜底：白名单（16 染料 + 7 斧头 + 墨囊/荧光墨囊/蜜蜡）之外，`*_dye` 与 `*_axe`（排除镐）也认，免得新版本加颜色/材质时静默放行。
- 商店牌还在但店停摆时（附着格不是可用容器 / 牌面已不严格合法）：能进原版编辑界面的判据与提交阶段相同（店主与 OP 放行，店坏了要能修回来），其余人拦下并分别提示「商店箱不在了」或「写法现在不算有效商店」。
- 管理员商店的身份判定：`shopActorOf` 新增 `adminShop` 入参，管理员店只有现任 OP 算管理身份（牌上记的创建者撤权后不再有权限，DESIGN §11）。
- 归属交接改成 4 条小环：两个玩家同一 tick 各改各的牌时，单槽会把前一个人的交接顶掉（那条 After 就什么也不写，引擎没写 XUID 的牌子会静默变成无主牌）。
- `tests/shop/SignProtectionTest.cpp`：物品分类、身份与权限、交互决策（含背面、潜行、上蜡、斧头没蜡）、提交闸与文案，共 26 个中文用例。
- 商店箱保护（`src/shop/ContainerProtection.*`、`src/mod/ContainerProtectionListener.*`）：`PlayerOpenContainerBeforeEvent` 拦非店主开箱（店主受 `allowOwnerOpenChest` 开关、OP 恒可），`PlayerAttackBlockBeforeEvent` + `PlayerDestroyBlockEvent` 拦非店主拆箱/拆牌；`attachedShopSignsAt` 反查「这一格容器被哪些商店牌绑着」（双箱两半都看，同一块牌只收一次）。
- 容器保护的判据与牌面防护对齐：牌面用 `looksLikeShopSign`、不看 `shopContainers` 白名单——改配置或牌子变脏都不该让商店箱失去保护；普通箱子与普通告示牌返回空，原版照常。
- 拆商店箱（双箱任意一半都算）时先按原版语义打掉绑着的商店牌（有掉落），商店随之失效；拆掉不挨着牌子的那一半也不会留下一个带着半份库存继续营业的商店（DESIGN §13 第 34 条）。
- 无主牌（`mLastEditXuid` 为空）的容器操作 fail-closed：开箱子/拆箱子/拆牌子只给 OP（与牌面文本的 fail-open 故意不同，DESIGN §9.2 新增的那一表）。
- `config.json` 新增 `allowOwnerOpenChest`（默认 `true`）：关掉后连店主也开不了自己的商店箱（提示指向这个开关），只有 OP 能开；不影响拆箱。
- `tests/shop/ContainerProtectionTest.cpp`：开箱/拆箱/拆牌的权限与提示文案，共 13 个中文用例。
- 破坏防护（`src/shop/DestructionProtection.*`、`src/mod/DestructionProtectionHooks.*`）：爆炸 / 活塞 / 凋灵 / 火焰都吞不掉商店箱与商店牌，而旁边的方块照常被炸、被推、被烧。粒度是**方块**：爆炸把商店方块从 `ExplosionStartedEvent::mBlocks` 里剔除、活塞在 `_attachedBlockWalker` 遇到商店方块返回 false、火焰在 `FireBlock::checkBurn` 跳过这一格、凋灵破坏期间由 `Level::$destroyBlock` 逐格拒绝。故意偏离 Java 版（ADR-0004）。
- 凋灵防护不照搬 ChestLock 的「提前返回」：26.51.8 的 `WitherBoss::_destroyBlocks` 只拿到整个 `AABB`，提前返回等于取消整场破坏（旁的方块也一块不掉），违反 ADR-0004。改成「`_destroyBlocks` 只置一个线程局部的『凋灵正在破坏』标志后放行原版，由 `Level::$destroyBlock` 在标志期内逐格拒绝」——爆炸、玩家拆除、我们主动打商店牌都不受这个标志影响。
- 拆商店牌**依附的方块**（`ShopBlockTouch::BreakSignSupport`、`src/mod/ContainerFinderBds.*` 的 `shopSignsWithAttachedCell`、`isShopBlockAt`）：拆掉那块墙/桶子/方块会让牌子物理掉落、商店消失，非店主因此能绕过“不能拆牌”。放行条件与拆牌相同（店主与 OP），接在 07 已有的 `PlayerAttackBlockBeforeEvent` / `PlayerDestroyBlockEvent` 监听里（不给同一事件注册第二个监听）；牌挂在石头墙上也算——牌面判据不看附着容器。
- `src/shop/DestructionProtection.*` 把两个可判定的部分做成纯函数：`cellsToSpare`（哪些目标格子要放过：只放受保护的，其余照炸；爆炸用它筛方块列表，凋灵/火焰每次只有一格，直接问判据）与 `signsAttachedTo`（哪些牌附在目标格上：站立看下方、吊牌看上方、墙牌看向背面的反方向）。
- `tests/shop/DestructionProtectionTest.cpp`（新）：剔除语义与依附几何，共 7 个中文用例；`ContainerProtectionTest.cpp` 补 4 个 `BreakSignSupport` 的用例。
- 内容防偷（`src/shop/ContentProtection.*`、`src/mod/ContentProtectionHooks.*`）：漏斗方块、漏斗矿车、铜傀儡、投掷器都动不了商店箱里的东西。四条路径共用同一个判据（`shouldSkipContentTransfer`：目标容器是商店箱就跳过**这一次**搬运）+ 同一个「这一格是不是商店箱」（`isShopBlockAt`），不看白名单、也不看店主——商店箱的内容只能通过交易流动，店主也不能拿漏斗替自己开箱。粒度是「这一次搬运」，不是停掉装置。
- H2a 漏斗（`Hopper::_tryPullInItemsFromAboveContainer` / `_pushOutItems`）：抽判漏斗正上方那一格，塞判 `attachedFace` 那一格；方块漏斗与漏斗矿车共用 `Hopper` actor 的同一套逻辑，两条 hook 同时覆盖两种形态。
- H2d 投掷器（`DropperBlockActor::pushOutItems` + `DropperBlockActor::getContainerAt`）：入口只开一个「这一次目标是商店箱」的 thread-local 作用域，由内层容器查找交不出那一格，原版因此把物品**喷到地上**，而不是把投掷器停掉；只要这一次没真正挡到（内层没被问过，或问了但坐标对不上）就打一条 warn 并说明是哪种失效，不许静默。
- H2c 铜傀儡（`TransportItemsGoal::_executeEvent`）：只丢掉「即将成功的取 / 即将成功的放」两个事件，`canUse` / `canContinueToUse` / `tick` 一个都不碰——不取消 goal，所以不会因为 goal 被取消而失去运输行为。这个落点是否真是搬运闸门、丢掉事件后 goal 会不会停在交互中，都属于只能真机确认的部分（已转 12）。
- `src/shop/ContentProtection.*`（新，纯逻辑）：`cellInDirection`（`facing_direction` 0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东 → 目标格，与 `neighboursOf` 共用同一张偏移表）、`cellAbove`、`shouldSkipContentTransfer`。方向几何与「跳不跳」都有 doctest 覆盖（`tests/shop/ContentProtectionTest.cpp`，5 个中文用例）。
- `src/mod/ContainerFinderBds.*` 新增 `facingDirectionAt`：投掷器的朝向只能从方块状态读，状态名的命名空间差异复用既有的 `readIntState`，不另写一份查表。
- `src/mod/HookSlots.*`（新）：把「一张 `HookSlot{name, attach, detach, attached}` 表 + 挂上 / 逆序解开」收成一份实现，08 的破坏防护与 09 的内容防偷共用；`currentShopConfig()` 也提到 `Shop.h`，两条保护路径的「配置缺失就放行」只有一处定义。
- 经济后端探测（`src/shop/Economy.*`、`src/mod/EconomyService.*`）：`auto` 按 `legacy_money → scoreboard → null` 的完整顺序探测（**`auto` 已于 2026-11-01 删除，见「变更」**），每一级降级都带原因进日志（`配置 auto → 选用 scoreboard（跳过 legacy_money：进程里没有 LegacyMoney.dll）`）；候选顺序与「给定各候选可用性选中谁」是纯函数 `selectBackend`，有单测覆盖。`ServerStartedEvent` 监听在世界就绪后立刻探一次，首次动钱时还有兜底；结论（含「一个后端都没有」）会缓存，换环境后需重载模组。
- LegacyMoney 后端（`src/mod/LegacyMoneyEconomy.*`）：用 `GetModuleHandleW(L"LegacyMoney.dll")` + `GetProcAddress` 解析 `LLMoney_Get/Set/Add/Reduce/Trans`，**不链接、不 include 它的 `LLMoney.h`**（那会在 `Shop.dll` 里留导入项，没装 LegacyMoney 的服务器直接加载失败；构建后已确认导入表里没有它）。每笔交易只走一次 `LLMoney_Trans`（SQLite 事务，天然原子，`pay_tax` 与第三方拦截都在它内部处理），`to` 为空 = 付给系统（销毁）。`LLMoney_Get` 的 `-1`（空 XUID / 数据库异常）按「读不到」处理；DLL 运行期被卸载时按「读不到 / 拒绝」处理并报一次 error，不去调悬空的函数指针。
- 记分板目标可自动创建（`ScoreboardEconomy::create(name, createIfMissing, issue)`）：`economy.createObjectiveIfMissing`（默认值 2026-11-01 改为 `true`）打开时用引擎自己的 `dummy` 判据（`Scoreboard::getCriteria` + `addObjective`）建目标，建不出来就打 `ObjectiveCreateFailed` 并降级。
- 离线店主按后端能力判定（`economy::ownerUnreachable`，纯函数）：点牌时藏按钮、`prepareSell`、`planTrade` 三处共用同一个判据；`economy.requireOwnerOnline`（默认 `true`）只对不支持离线的记分板有意义，关掉并不会让记分板获得给离线玩家记账的能力。
- 商店税（`tax.shopTaxRate`，默认 `0`）：店主承担，`总额 × 税率` 向下取整，税为 0 完全跳过第二笔。买入时从店主收入里扣（买家支出不变），卖出时店主另付一笔；收税排在物品那一步之后，所以任何失败点的补偿都精确，补偿再失败才升级为 `Broken`。税率只在能「把钱付给系统（销毁）」的后端上收得走（目前只有 LegacyMoney），记分板上不生效并在启动日志里说明。`shopTaxRate > 0` 时启动无条件 WARN 要求把 LegacyMoney 的 `pay_tax` 设为 0（那个值没有任何导出能读，只能提醒）。卖出方向的份数上限按含税成本算（`affordableUnitsWithTax`）。
- `tests/shop/EconomyTest.cpp` / `TradePricingTest.cpp` / `TradePlanTest.cpp` 补 17 个中文用例：探测顺序与降级原因文案、探测计划、离线判据矩阵、转账参数校验、税率舍入与边界（含 NaN）、含税份数上限、成交方案里的税额与「税压在谁身上」的两种形状。
- 管理员商店（`src/shop/Economy.*`、`src/mod/TradeService.*`、`src/shop/SignProtection.*`）：OP 建的管理员店现在真的能买卖了——资金对手方是**系统账户**（空账户键），买入时钱被销毁、卖出时凭空记到玩家账上（`EconomyProvider::supportsSystemAccount()`，LegacyMoney 走 `LLMoney_Add` / `LLMoney_Trans(to="")`，记分板走 `add` / `reduce`）。库存仍然来自容器，卖光就是没货；管理员店不收商店税、不吃离线策略，`shopSideAccount` / `shopSideFunds` 把这两条收成纯函数。
- 管理员店的表单角色一律是访客（`shopFormRoleOf`）：OP 在这里也是买家，「不能跟自己交易」不适用；管理能力（改牌、开箱、拆箱）仍由 `shopActorOf` / `canManageShop` 判，与表单按钮彻底解耦。
- `checkTransfer` 改成问后端对象（`checkTransfer(provider, from, to, amount)`）：金额、空付款方（＝从系统付款，需要系统账户能力）、空收款方（＝销毁，销毁能力或系统账户能力任一）、自转自，四条校验与文案仍只有一份；基类 `transfer` 补上两个系统账户方向的实现（凭空记账 / 销毁）。
- `tradeTaxPlan` 新增 `adminShop` 入参：管理员店两个方向都不收税。卖出份数表单在管理员店不再把「店主余额」写进上限来源（`UnitsForm` 的 `capHintOf`）。
- `tests/shop/EconomyTest.cpp` / `TradePlanTest.cpp` / `SignProtectionTest.cpp` / `ShopInfoTest.cpp` / `UnitsFormTest.cpp` 补 6 个中文用例：系统账户的两个方向与能力组合、管理员店的资金侧纯函数、管理员店免税、表单角色、管理员店按钮与限额文案。
- 管理表单（P1 票 01，`src/shop/ManageForm.*`、`src/mod/ShopForm.*`）：点牌后「管理商店」不再是一句「待实现」——点进去是管理表单，本票先接上「改买入价」：`CustomForm` 一个输入框（初值 = 当前买入价，只收非负整数或留空），提交后牌面第 3 行规范化回写，并把这一格发给整个维度（`resyncBlockActorToDimension`）。写入用 `setMessageForServerScripingOnly`，普通店传店主 XUID、管理员店传空串，归属不变。
- 管理入口与交易角色解耦（`shopFormButtons(data, role, canManage)`）：`canManage` 为真时**总是**给管理入口，与 `unavailableReason` 无关（否则管理员店一停业就永久无法恢复营业）；买卖按钮仍只看 `role` 与 `unavailableReason`。
- 管理表单的校验只在一处（`withBuyPrice`）：非负整数 / 留空、两侧都清空则拒绝（否则第 3 行变空、箱子失去保护）、改完后卖出价高于买入价则拒绝；判据与手写编辑共用新抽出的 `sellPriceHigherThanBuy`。失败只提示，牌面一字不改。
- `tests/shop/ManageFormTest.cpp`（新）+ `ShopInfoTest.cpp`：改价合成（正常 / 留空 / 非法字符与溢出 / 两侧都空 / `S > B` / 配置关闭该限制）、管理表单正文与上蜡提示、管理入口与 `unavailableReason` 的四种组合，共 17 个中文用例。
- 管理表单接上「改卖出价」与「改每份件数」（P1 票 02）：管理表单变成三个入口，按钮文案带当前值（`改卖出价（现在 $ 5 / 份）`、没有卖出价时写「不收购」、`改每份件数（现在 64 个）`）。卖出价留空 = 这一侧不收购（第 3 行只剩 `B 10`）；件数只收 `1..maxShopAmount`，越界直接给「一份的件数要写 1 到 N」的原因，**不悄悄夹到边界**。
- `withSellPrice` / `withAmount` 与 `withBuyPrice` 共用同一套输入解析与同一份收尾校验：买卖两侧走 `parsePriceInput` + `validatedEdit`（两侧都清空拒绝、卖出价高于买入价拒绝），换一侧只换「落到哪个字段」与提示里的名字；件数直接问牌面第 2 行那份 `parseAmountLine`（“什么叫合法件数”只有一份），范围文案由新的 `amountOutOfRangeMessage` 供手写编辑与管理表单共用。两侧都清空的提示补上「想关店请拆牌，或者停业（牌子与库存都留着）」，并说明箱子会失去保护（spec D6）。
- 三张单输入框表单与提交链只有一份（`src/mod/ShopForm.cpp` 的 `manageInputField` / `openManageInputForm` / `submitManageEdit`）：控件名、三句文案与初值按 `action` 取，`重新取牌 → 重新解析 → 重新鉴权 → 写牌 → 发整维度` 是同一段（提交只接「合成函数 + 成功提示」，不把表单文案带进去），`ManageSellPrice` / `ManageAmount` 不再在 `onButton` 里占位返回。输入读回仍只走 `get_if<std::string>`，读不到报「读不到这个输入框的值」而不是当 0 / 当留空。
- `tests/shop/ManageFormTest.cpp` 补 16 个中文用例：卖出价正常改 / 留空 / 只写空白 / 非法输入 / `S > B`（两边相等放行、买入价为空时不比）/ 两侧都空、件数 `1` 与 `maxShopAmount` / `0` 与 `maxShopAmount+1` / 留空与非整数、改完 `formatShopSign` 与手写编辑同格式、改完仍是商店牌、三个按钮的当前值与缺价文案。
- 停业 / 恢复营业（P1 票 03）：管理表单第四个按钮，按当前状态只给一个（「停业」/「恢复营业」）。停业状态写在第 3 行末尾的 `:x`（`B 10 : S 5 :x`、单侧 `S 5 :x`），价格、归属、库存与保护一个字不变，恢复营业就是去掉那一段——「原价一分不差」因此是结构上成立的。牌子即数据源，重启后仍然停业。
- 价格行解析改成「**先剥离最后一段**」：末尾一段 trim 后等于 `x`（大小写不敏感、`: x` 也认）就判停业并剔除，剩下的再按原文法解且必须至少剩一个价；`segments.size() > 2` 的检查挪到剥离之后（否则 `B 10 : S 5 :x` 会先被拒）。规范化恒输出 `:x`，所以解析与格式化互逆，手写编辑写 `:x` 同样生效且不会被抹掉。
- `TradeRejection::ShopClosed` + 「这家店暂停营业」：判据在 `planTrade` 最前面（比任何上限与输入校验都优先，两个方向同一句），提交阶段再判一次（防停业之前打开的旧表单）。停业时点牌正文多一行同一句、一个买卖按钮都不给。
- 管理入口在停业时照给（回归断言：管理员店的 OP 在交易上是 `Visitor`，入口一消失就永久停业，只能拆牌）。
- 管理表单四个动作收在 `applyManageEdit` 一处（spec「Testing Decisions」的那条缝）：`withBuyPrice` / `withSellPrice` / `withAmount` / `withToggledClosed` 都是它的薄包装，校验（输入非法、两侧都清空、`S > B`）只有一份；停业不豁免其中任何一条。停业这一项没有输入框，但仍走 `submitManageEdit` 的同一条链（重新取牌 → 重新解析 → 规范化 → 写牌 → 发整维度），没有第二条写牌路径。

### 修复

- **交易冷却与冻结键补上维度**（`TradeGate` / `TradeService`）：`Cell` 只有坐标，主世界与下界/末地同坐标的两家店以前会共享 250ms 冷却，补偿失败的冻结还会跨维度连坐（提示写「上一次交易未完成」，玩家根本想不起是哪一次）。键改成「玩家 + 维度 + 坐标」，`tests/shop/TradeGateTest.cpp` 补一条跨维度用例。
- **冻结名单不再由 `disable()` 主动清除**：`/ll reload` 与关服都走 `disable()`，以前运维为了改一个无关配置重载一次，就把「钱货还没对上」的交易对静默解冻了。现在只在卸载时打一条 WARN 说明还有 N 对没核对。注意这只能挡住「主动清」——加载器若把 DLL 整块卸载再载入，静态对象仍会消失；要真跨过那个边界必须把冻结记录落盘（未做）。
- **监听器部分注册失败后能补**：`ContainerProtectionListener` 的三条保护改成逐条判空注册（原来任一非空就整体跳过，半边保护永远修不回来）；`SignEditListener` 的 Before/After 改成「全有或全无」——半挂会让归属交接留在 4 槽环里被下一次编辑捡走，把店主写到不相干的人头上。
- **牌子编辑读不到实体时不再写空串**：以前 `sign == nullptr` 会用 `""` 兜底，等于把玩家牌面抹掉；现在放弃这次编辑并打一条 warn。
- **交易日志的两处无谓 syscall 去掉了（实测单笔 634 µs → 326 µs，约 -49%）**：`create_directories` 以前每笔都逐级 `stat` + `CreateDirectoryW`（日志目录固定两三个，改成进程级只建一次，打不开时清记录重试一次）；回滚点以前用 `std::filesystem::file_size`，它在 Windows 上是**另一次完整 open/close**（`CreateFileW` + `GetFileSizeEx` + `CloseHandle`），改用打开后 `seekp(0, end)` + `tellp()`（`SetFilePointerEx`，几乎免费），回滚语义不变。剩下的 326 µs 全是 `ofstream` 的 open/close，只有缓存句柄才能消掉，暂不做。
- **日志压缩挪到启动时**：以前每写一行流水就扫两个目录、必要时当场同步 gzip 一整个旧文件，那笔 CPU 与磁盘 I/O 全落在玩家成交那一拍的 tick 线程上。现在只在模组启用时压一次（压了几个文件打一条 info），成交路径上只剩「开 → 写 → flush → 关」；失败的原文件留给下次启动重试。
- **商店税：税率只支持千分位，税额下限 1**：`tax.shopTaxRate` 不是 `0.001` 的整数倍、或落在 `[0, 1)` 之外时**不拒绝加载**——启动 WARN 一次、本次运行按 `0` 处理（这个配置失效），交易与保护照常；合法值加载后对齐到千分位网格，避免计价、`/shop status` 与流水各自带浮点尾巴。`shopTax` 在向下取整之后取 `max(1, ...)`：以前不足 1 的税归零，等于把大单拆成小单就能免税；现在 100 元 5% 仍是 5，拆成 20 笔 5 元则每笔按 1 收、共 20。新增纯逻辑测试 `isTaxRateOnGrid` 与「拆单不能把税拆没」用例。
- **`/shop status` 在配置不可用时不再早退**：以前配置坏了就只打一句「模组没有启用」然后返回，而那一刻恰恰是**交易与全部保护都没生效**的时候，hook 表是唯一能说明「到底挂上了没有」的东西。现在只跳过依赖配置的几行，hook 表、经济后端、冻结数、调试开关照打。
- **`/shop status` 增加「冻结中的交易对」一行**：`compensation_failed` 之后钱货没对上的交易会被冻在内存里等人工核对，以前这个数字只在关服 WARN 里出现，游戏内看不到。
- **`stripColorCodes` 按 UTF-8 跳过颜色码（真 bug）**：`§`（`0xC2 0xA7`）之后无条件再跳 1 字节，遇到 CJK（3 字节）就切掉高位，剩下半个非法 UTF-8 继续流进牌面解析、消息与流水。现在只在该字节是 ASCII（真正的格式码）时才吃，`§只有半个` 会完整保留「只有半个」。
- **`check_pure_logic_headers` 覆盖 `ll/` 与 `mod/`**：以前只拦 `mc/`，LeviLamina 或本模组适配层的头能溜进 `src/shop`，纯逻辑层就不再能被 `ShopTests` 独立编译。
- **`warnOnce` 的抑制键改用 `(种类, 路径)` 对**：不再为每个失败拼一次 `"种类|路径"` 的 UTF-8 字符串（那是一次宽字符转换）。
- **`decimalText` 不再把极小值显示成 `0`**：`std::to_string` 只给 6 位小数，`0.0000001` 去零后成了 `"0"`、看着像没配。现在退化成 `"0"` 而原值非 0 时改用 `%.3g`。
- **新增 `/shop reload`：只重读 `config.json`，不卸载模组**：`/ll reload Shop` 会把监听器、冻结名单、经济后端全部拆掉重来，改一个价格上限不该付这个代价。新命令只做「读文件 → 校验 → 原地替换 `mConfig` → 重建冷却窗口与后端选择」，**解析失败时保留当前配置**（热重载读到坏文件就把配置清掉，等于保护全关）。hook 里一律现取配置，所以下一次交互就用新值。
- **牌面配置（`ShopSignConfig`）改为 `Shop` 里缓存一份**：`maxShopAmount` / `adminShopName` / 价格规则以前每次用 `toSignConfig` 现算，而它带一个 `std::string`——漏斗、投掷器、铜傀儡的 hook 每 tick 都要拷一次。现在 `load()` 与 `/shop reload` 各算一次，热路径只读指针；缓存为空时（模组未加载、单测）仍退回现算。
- **漏斗 / 投掷器 / 铜傀儡的每 tick 判据不再扫六邻居**：这四条 hook 是每 tick 跑的（漏斗 20 次/秒，一片农场的漏斗数会把成本乘起来），以前每次都走 `isShopBlockAt`——一次方块实体查询之后无条件扫六个邻居、再拷两份 `ShopSignConfig`（含 `std::string`）。现在走新的 `isShopContainerBlockAt`：先一层「这一格是不是箱子/木桶」的粗筛，漏斗链、熔炉、投掷器自身都在那里被挡掉。判据仍是同一处组装，破坏防护与拆除路径继续用 `isShopBlockAt`（它们是玩家操作频率，还要认商店牌本身）。
- **日志压缩只扫 `transactions/` 与 `discarded/`**：`logFileDate` 只认裸 `YYYY-MM-DD.jsonl`，`logs/` 根下服主自己的 `backup-2026-11-01.jsonl` 不会再被压缩并删原文件（旧布局的搬家仍由 `relocatedLogFile` 负责）。
- **日志写失败会回滚半行**：磁盘满时 `<<` 可能已经写进半条 JSON，NDJSON 从那一行起整份读不出来。现在按写入前的文件大小截回去，并单独提示「这一批丢了」；`warnOnce` 的键改成「种类 + 路径」，`/ll reload` 时复位，`transactions/` 与 `discarded/` 的失败不再互相吃掉。
- **配置写入与校验收紧**：`config.json` 的回写条件从 `merged != user` 收窄为「缺键 / version 不同」（不再被用户写的 `null` 触发），回写前备份成 `config.json.bak`；`maxShopAmount ≤ 32768`、`shopInteractionCooldownMs ≤ 60000`、`compressAfterDays ≤ 3650` 有了上界（分别对应表单 int 溢出、交易被静默拦下、`chrono::days` 溢出）；`tax.taxDestination` 写了 `void` 以外的值会 WARN 而不是静默不生效。
- **`/ll reload` 后 `/shop status` 不再显示旧后端**：`resetEconomy()` 补清选型文本，`/shop status` 改用 `economyUnresolvedReason()` 区分「世界未就绪」与「世界已就绪但还没解析」。
- **`pay_tax` 告警不再只在开税时出现**：只要 `economy.backend = legacy_money` 就无条件 WARN——不开商店税时补偿退款同样会被抽一次，退款按原金额反向转就会让一方固定亏一截。
- **表单件数改用 `int64` 算**：`UnitsForm` 的「共 N 个」与提交时的 `expectedItems` 共用新的 `itemsForUnits`，合法配置范围内不再有 `int` 溢出。
- **静默丢弃的提交有了痕迹**：交易与管理表单在「玩家换维度 / 下线 / 配置不可用」时不再无声 `return`——换维度会给一句可见提示，其余各留一条 debug 日志；`/shop status` 的 hook 表新增「曾尝试挂上，现已解绑」，`detachHooks` 之后不再被误报成「从未注册」。
- **配置非法时的后果写明白了**：`load()` 失败现在明确说「交易、商店箱保护、破坏防护、内容防偷全部不会启用」，不再只是一句「配置不可用」。

### 变更

- `ShopOwner::ensureOwnerRecorded` 改名为 `recordShopOwner`：归属在牌子第一次成为商店时写给编辑者，之后改牌面不改归属（After 阶段按 Before 记下的归属写回）。否则 06 的「OP 可以代改别人的牌面」会顺带把店主换成 OP，把原店主锁在自己的店外。
- `TradePricing::stockUnits` 改名为 `unitsFromItems`：商店箱的货与背包容量共用同一个「件数 → 份数」算式。
- `src/shop/BuyForm.*` 改名为 `src/shop/UnitsForm.*`：份数表单与确认表单只有一份实现，正文与措辞按 `TradeDirection` 参数化。
- `TradePlan::planBuy` 并入 `TradePlan::planTrade`（`BuyRequest` / `SellRequest` 合并为 `TradeRequest`，角色由调用方装）：买卖不再各写一份份数算法。
- `ShopSnapshot` 拆成「只看世界」的 `readShop` 与「看视角 + 读余额 + 定角色」的 `inspectShop`，交易路径复用前者。
- `ContainerFinderBds` 导出 `AttachedContainer` / `mergedSlots` / `sameShopItem` / `firstProductStack`，「合并槽位的顺序与边界」与「严格同种比较」各只有一处定义。
- `ContainerFinderBds` 收掉三处重复形状：`containerAt`（容器两半 + `ContainerRef`）、`forEachSignAround`（两半 × 六面）、`shopSignOwnerHere`（牌子 → 店主，`looksLikeShopSign` 判据）。反查与开店路径共用它们。
- `xmake.lua`：移除 server/client 双目标，固定为服务端模组；版本号由 `modpacker` 显式指定为 `0.1.0`。
- `manifest.json`：移除未被替换的 `platform` 占位符。
- CI：`build.yml` / `release.yml` 只构建服务端产物。

### 变更

- **删掉 `economy.backend = "auto"`，默认值改为 `legacy_money`，并取消所有跨后端自动降级。**
  `auto` 的含义是「LegacyMoney 探不到就用记分板」，而它又是默认值：服主的 LegacyMoney 一旦因版本
  不兼容没加载（导出缺失），本模组会**默默换一套账本**，玩家余额看起来全变 0，日志里只有一条
  info。现在合法取值只有 `legacy_money` / `scoreboard` / `null`，一个后端只探它自己，探不到就
  落到空实现并打 error（含「改成哪个值能解决」的建议）。老配置写 `auto` 会拒绝加载并给出专门的
  迁移提示。`economy.createObjectiveIfMissing` 默认值同时从 `false` 改为 `true`（选了记分板就是想要
  记分板记账，不必为一个 `dummy` 目标再手打命令）。
- 顺带清掉 `SignInteractListener.cpp` 里 `sign_decision` 重构后遗留的未用函数 `actionName`。

### 变更（P1 票 01：管理员店买入无限）

- 纯逻辑新增 `StockView`（`src/shop/StockView.h`）：`makeFinite(n)` / `makeUnlimited()` 两个构造口，
  无限时 `units` 为空——**不再用 `INT_MAX` / `-1` 当哨兵参与 `min`**。`TradeRequest::goodsUnits` /
  `roomUnits`、`ShopInfoData::stockUnits`、`TradePricing::tradeUnits`、`maxTradeUnits` 全部换成它，
  「无限那一项不参与 `min`」只在 `tradeUnits` 一处实现。既有测试的 `goodsUnits = 5` 靠隐式转换照旧可读。
- 管理员店（`readShop`）的库存一律 `makeUnlimited()`；买入执行路径跳过「从箱子扣货」，
  样品槽前后逐字段相同；表单份数行走同一个 `stockUnitsText`，无限写「无限（管理员店，永远有货）」。
  卖出方向的 `heldUnits` 仍是有限（无限化就是复制物品），商店箱空间这一项留给票 02。
  余额、`maxShopAmount`、背包空间三个上限在无限下照旧生效。

### 变更（P1 票 02：管理员店卖出无限 + 销毁留档）

- `ShopInfoData::boxRoomUnits` 换成 `StockView`：管理员店的模板箱是 `makeUnlimited()`（收进来的货销毁，
  永远收得下），普通店仍是准确数字。表单的「商店箱还能装」行与上限来源同理，不再列一项不存在的限制。
- 卖出执行路径对管理员店跳过「放进箱子」这一步：先给钱、再从背包取货、销毁、完成；模板箱与样品槽一件不动。
  补偿与普通店同一套（取货失败 → 背包快照回写 + 退款；补偿失败 → fatal + 通知 OP + 冻结）。
- 新增纯逻辑 `DiscardRecord`（`src/shop/DiscardRecord.h`）：一行一个被销毁物品堆的 NDJSON，
  键序固定 `ts` / `shop` / `player` / `item{id,aux,name,nbt}`，字符串转义与 UTF-8 原样写都有单测。
- 新增 `src/mod/WallClock.*`（本地 ISO-8601 带时区偏移）与 `src/mod/DiscardLog.*`：
  SNBT 用 `ItemStack::save(SaveContext{Clone})->toString()` 生成，记录在 `Done` 之前造好、
  在 `finishTrade` 里 `Done` 之后落盘到 `plugins/Shop/logs/discarded-YYYY-MM-DD.jsonl`；写失败只 warn。
  （`transactionLog.enabled` 这个开关属于 `p1-transaction-log`，等它落地后接管这条通道。）

### 变更（P1 票 03：样品槽语义与「还没设商品」）

- 新增纯逻辑 `SampleSlot.h`：`sampleSlotIndex(slots, isEmpty)` = **第一个非空槽位**（不是固定 0 号槽）。
  `src/mod/ContainerFinderBds` 的 `firstNonEmptyStack` 改成只提供「第 i 格空不空」，
  「第一个非空」这条规则因此只有一份实现，且能单测（0 号空 / 1 号有 / 全空 / 其余格忽略）。
- 新增 `unavailableReasonForEmptyBox(bool adminShop)`：普通店「商店箱里没有货，补货后恢复」与
  管理员店「这家管理员店还没设商品：往商店箱里放一件样品，它就卖这个」是**两条不同的原因**，
  `readShop` 与提交路径的预校验都走它；空箱时买卖按钮一个不给、管理入口照给。
- 模板箱语义写进 DESIGN §7.4 / §5.3：不参与库存增减、卖进来的物品销毁（留档）、其余格子忽略、
  不做持久化（换商品 = 换第一格）；GLOSSARY 新增「样品槽」。

### 新增（P1 票 01：每笔成交落一行流水）

- 新增纯逻辑 `src/shop/TradeRecord.*`：`TradeRecord` + `composeTradeRecord` / `tradeRecordJson` /
  `transactionLogFileName` / `tradeRecordProblem`。一行一个 JSON 对象，键序固定
  `ts` / `event` / `result` / `dir` / `shop{dim,x,y,z}` / `owner{xuid,name}` / `admin` / `backend` /
  `player{xuid,name}` / `item{id,aux,name}` / `units` / `items` / `unitPrice` / `gross` / `tax` / `total`，
  结尾一个 `\n`、UTF-8 无 BOM、中文不转义——`jq -c .` 与 `duckdb read_json_auto` 都能直读，不需要先处理。
- `TradeResult` 带 `logFacts`（事实）+ `logReason`（原因短名），`finishTrade` 一个漏斗里按下
  `Done → ok` / `Rejected → fail` / `Broken → frozen`（`NeedsConfirm` 不是终态，不写），
  在 `player.sendMessage` 之后、DiscardLog 之前落盘到 `plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl`。
- 新依赖 `nlohmann_json 3.12.0`（header-only；LeviLamina 不传递它，`Shop` 与 `ShopTests` 两个 target 各自声明）。
  `src/shop/JsonLine.*` 是唯一的单行 NDJSON 出口（`dump(-1, ' ', false, replace)` + `\n`）。

### 新增（P1 票 02：失败与冻结也留痕）

- 拒绝与冻结也各是一行：`reason` 是可查询的英文短名（`no_money` / `no_stock` / `no_space` /
  `owner_offline` / `shop_closed` / `over_limit` / `invalid_request`，以及只存在于适配层的
  `shop_missing` / `frozen_pair` / `compensation_failed` …），**中文文案（会变）不进流水**。
  `frozen` 行与 `fatal` 日志必然成对（同一个 `broken()` 里写），同一对「玩家 + 商店」被冻结后的后续提交
  记 `fail` + `frozen_pair`，不会把一次事故记成两次。
- 免费店（0 元成交）仍是一行 `ok`：它证明那次交接真的发生过；一份都没成交的 `fail` 行里份数与金额写 `0`，
  而不是省掉字段。
- `DiscardRecord` 的 JSON 拼装从手写转义改成 `nlohmann::ordered_json` + 共享的 `jsonLine`，
  与业务流水共用同一份转义规则（既有 5 条快照断言逐字节未变，改动因此是可验证的）。
- 新增 `src/mod/LogFile.*`：`transactions-*` 与 `discarded-*` 的**唯一**落盘入口（`appendLogLines`），
  建目录 / 追加 / `flush` / 关文件失败只 `warn` 且同类失败同进程只提示一次，交易照常成功。

### 新增（P1 票 03：压缩与配置，永不删除）

- `config.json` 新增 `transactionLog`：`enabled`（默认 `true`）、`compressAfterDays`（默认 `7`，`0` = 永不压缩，
  负数加载即拒绝）。`enabled = false` 时 `transactions-*` 与 `discarded-*` **一起停**（开关只在一个入口判）。
- 新增纯逻辑 `src/shop/LogRotation.*`：从文件名解析日期（真实日历校验，闰年不靠手算）、
  `shouldCompress(fileDate, today, days)`、`compressedLogFileName`（已 `.gz` 原样返回，因此幂等）、
  `isoDateOf`（「`ts` 前 10 位就是日期」只有这一份实现）。
- 压缩用 zlib 流式读写成 `*.jsonl.gz`（新依赖 `zlib 1.3.2`，只挂在 `Shop` 上——纯逻辑测试不需要 zlib）。
  **永不删除**：只压不删，清盘得服主自己 `rm`；顺序是「写 `.part` → 关好 → 原子改名为 `.jsonl.gz` → 删原文件」，
  中途崩服只会留下 `.part`（扫描认得 `.jsonl`，认不出它），不会出现「截断的 .gz 被当成已压过、原文件被删」。
  压缩失败保留原文件、下一轮重试；写成功之后顺手检查一遍目录，不额外开线程、不做定时任务。
- 日志按类型分目录（服主提议）：`logs/transactions/YYYY-MM-DD.jsonl` 与 `logs/discarded/YYYY-MM-DD.jsonl`。
  两类记录体量差两个数量级（流水一行几十字节、留档一行可能几十 KB），混在一层时 `du` 分不清谁在涨、
  `zcat` 查账会顺手解出几百 MB 的 NBT。压缩扫的是日志根目录连同两个子目录（写流水时顺手压留档，
  不必等那个目录自己有新写入）；旧布局平铺在 `logs/` 根下的文件在启用时自动搬进子目录
  （同盘 rename、不覆盖、失败只 warn、只认本模组写出来的名字、幂等）。
- `/shop status` 多一行「交易流水：开 / 关（compressAfterDays=N，目录 …）」；`README.md` 新增「交易流水」一节
  （字段含义、两个文件的分工、`jq` / `zcat` / `duckdb` 三个示例、两个配置项）。

### 修复

- **销毁留档的 `item.nbt` 写成了 `"4 entries"`**：`Tag::toString()` 在 BDS 里返回的是调试用的摘要
  （一个钻石正好 4 条 NBT：`Count`/`Damage`/`Name`/`wasPickedUp`），不是 SNBT。改用
  `Tag::toSnbt(SnbtFormat::Minimize)`（紧凑单行；默认的 `PrettyFilePrint` 会带换行），
  `SaveContext` 也从 `Clone` 改成 `SaveToDisk`——留档要的是「存档里长什么样」。
- **升级后配置里少一个新块，整个模组拒绝加载**：`ll::config::loadConfig` 只在文件缺 `version`
  或 `version` 与代码不同时才用默认值补齐，否则严格反序列化——给 `ShopConfig` 加 `transactionLog`
  之后，老 `config.json` 直接报 `missing required field "transactionLog"`，`Shop` 拒绝启用。
  现在加载一律**默认值打底 + 文件值覆盖**（纯逻辑 `src/shop/ConfigPatch.*`，8 个用例），
  缺的键按默认值跑并回写完整文件、启动日志写明补了什么；键名拼错会 WARN 列出被忽略的键；
  JSON 语法错仍拒绝加载且**不覆盖**用户文件。加新配置字段不再需要 bump `version`。
- **日志目录与文档不一致**：`logDirectory()` 用了 `getConfigDir()`（= `plugins/Shop/config`），
  流水实际落在 `plugins/Shop/config/logs/`，而 README/spec/`/shop status` 一直说的是
  `plugins/Shop/logs/`。改用 `getModDir() / "logs"`。
- **告示牌没有「除蜡」这回事**：早期把 Java 版铜块的规则套到了告示牌上，判出「斧头 = 除蜡」并在提示里教玩家「先拿斧头除蜡才能改」。实际原版告示牌上蜡后没有任何去除手段（Mojira MC-261313 判 Works As Intended；Wiki 写「若要修改则需破坏后重新放置」），
  所以那条 `SignTouch::Axe` 分支在基岩版永远不会被原版触发，提示也是错的。现在斧头就是**普通物品**（点牌等于空手点牌），
  上蜡的牌对所有人只读，改价只能拆牌重立或走将来的管理表单（`DESIGN §9.2` 的「上蜡不可逆」）。
- `/shop status` 之前把「世界还没起来」和「解析没跑」写成同一句「尚未解析（世界未就绪）」，
  真机上世界明明已就绪也是这句。现在两种情况分开写，并且经济后端改为**玩家进服时**也解析一次：
  `ServerStartedEvent` 在 LL 26.51.9 上并没有在就绪时刻送达（实测世界已就绪、玩家已进服仍显示未解析），
  只靠它等于等到第一次交易（`DESIGN §19.3`）。
- `/shop debug` 的枚举类字段（`plan` / `kind` / `dir` / `outcome`）打成了 `true`/`false`：
  `char const*` 唯一的隐式转换目标是 `bool`，`dbgField(key, "TakeOver")` 会静默选中 `bool` 重载。
  根因修法是给 `dbgField` 补一个 `char const*` 的**精确匹配**重载（精确匹配胜过布尔转换），
  所有传 C 字符串的地方自动正确，不必再逐处写 `std::string_view{...}`。
- **记分板后端支持离线店主**：以前记分板只能给在线玩家写分，店主要下线就没人能买。现在照 LSE
  `mc.addPlayerScore(uuid, …)`（文档写着「可修改离线玩家计分板」）的实现写分：
  `XUID → 玩家表 uuid → DBStorage 的 player_<uuid> → ServerId → 存档里的 UniqueID →
  scoreboard.getId(PlayerScoreboardId)`。用 ID 而不是名字，改名与重名都不受影响。
  `supportsOffline()` 改成 `true`，于是 `economy.requireOwnerOnline` 在当前两个后端上都已成空转开关
  （保留给将来的新后端）。边界：`getId` 返回 `INVALID`（这个人本世界还没有过记分板身份）时安全失败，
  按「读不到余额」拒，不硬建假玩家身份。
- **记分板后端现在也收商店税**（此前整笔跳过）。税本来就只需要「单边扣钱」——从买家扣掉、
  谁都不加就是销毁，不需要任何对手方账户；旧实现把它误判成「记分板没有系统账户」，于是
  在记分板上 `tax.shopTaxRate` 静默失效。现在两个后端在税上行为一致，换后端不会突然开始/停止收税。
- **记分板后端：从来没有分值记录的玩家不再被判成「读不到余额」**。老玩家可能连记分板身份都没有
  （`getScoreboardId` 返回 `INVALID`），旧实现直接当账户不可记账拒绝交易；现在照原版
  `/scoreboard players set` 的做法现场 `createScoreboardId`，新玩家的余额就是 0。
- **改牌被拒之后，这块牌子再也打不开原版编辑界面**（真机确认：「显示空白」与「改完还是原样」两种都中，
  必须退出重进）。根因是用 `event.cancel()` 拦下时客户端那次编辑没有正常收尾。现在**不再 cancel**，
  改成把 `event.text()` 设回**改之前那几行**——原版完整走完一次编辑，只是写回去的还是原内容；
  改之前是商店牌时同时把归属保住（`SignOwnerHandoff`），免得引擎把 `mLastEditXuid` 记成编辑者后
  OP 代改一次就把店抢走。
- **撤掉「编辑被拒后重同步」这一步**（真机 2026-11-01）：它在 20:00 那一版引入，之后真机报出
  「新放的牌子显示空白、打不开原版编辑界面、连点几次客户端掉线」。日志显示服务端把那块牌子判成
  `NotAShopSign`（我们既没拦也没 cancel），说明问题就在这次多发出去的包上，所以先退回上一版行为
  （牌面文字会留到重进为止），把这一段彻底改动留到有日志证据之后再做。
- **编辑路径补上日志**：`PlayerEditSignBeforeEvent` 的判定结果此前**一行日志都没有**，真机排查时只能猜。
  现在每次编辑打一行 `[shop/dbg] sign_edit outcome=NotAShopSign|Rejected|Normalized reason=…`。
- **重同步包不该自己拼 NBT**（真机第四报，比缺陷本身更严重）。`_getUpdatePacket` 在方块实体没标脏时
  返回空（刚放下、还没写过字的牌子就是这种），早先的退路是用 `save(..., SendOverNetwork)` 自己组
  `BlockActorDataPacket`——真机后果是：新牌子显示空白、**打不开原版编辑界面**、连点几次**客户端掉线**。
  现在只让原版生成包：`_getUpdatePacket` 为空就调告示牌的 `onChanged(region)` 标脏再问一次，
  还要不到就放弃；并对「同一玩家 + 同一格」加 250ms 节流，避免连点变成一串包。
- **建店失败（或改牌被拒）后，客户端仍显示“写进去了”**（真机 D1 全量验收发现）。玩家点确定的瞬间，
  客户端已经把这段文字写进本地那份牌子，`event.cancel()` 只挡住服务端写入，所以牌子看着有字、
  退出重进才发现是空的（正确的那个才是空的）。现在拒绝路径取消后立刻重发这一格；
  **规范化成功的编辑**也在 `After` 阶段补一次重发——否则客户端显示的是玩家敲的原文，
  而服务端存的是规范化文本（两者本来就不一样）。
- **拆商店牌 / 商店箱被拦下后，牌面文字在客户端消失了**（真机第二报）。`BREAK` 动作同样是本地预演的：
  客户端先把「牌子碎掉、文字没了」演出来，`event.cancel()` 只拦住权威状态，方块实体不会自动回来，
  于是出现「拆不掉，但牌子空了，退出重进才显示」。现在破坏路径的拦截也补一次方块实体重发。
  两处共用 `src/mod/BlockResync` 的 `resyncBlockActor(player, region, pos)`（首选
  `BlockActor::_getUpdatePacket`，拿不到就按 `SendOverNetwork` 自己组包，只发给动手的人），
  并把「任何 `cancel()` 拦下的、客户端做过的动作都要问一句客户端还对不对」写进 `DESIGN §9.2`。
- **拦下染色 / 墨囊 / 荧光墨囊 / 上蜡后，客户端仍显示“已经改过”的样子**（真机首测）。
  这五样在客户端是本地预演的，`event.cancel()` 只挡住了服务端写入，拦不住预演，所以玩家看到变色、
  退出重进才发现没生效。修法：拦下后把这一格牌子的真实数据再发一遍（
  `BlockActor::_getUpdatePacket`，拿不到则按 `SaveUseCase::SendOverNetwork` 存 NBT 自己组 `BlockActorDataPacket`），
  只发给动手的那个玩家。
- **份数表单选什么档都报「份数选择不对」**（真机首测碰到，整条买入链路卡死）。原因：LeviLamina 的 `CustomForm`
  下接框回传的**不是下标，而是选中项的文案**（`Dropdown::parseResult` 返回 `mOptions[index]` 字符串），
  而表单回调那侧只读 `uint64` 下标，永远读不到 → `choiceIndex` 恒为 -1 → `resolveRequestedUnits` 报错。
  修法：`src/shop/UnitsForm` 新增 `choiceIndexForOption`（`unitsFormOptions` 的逆运算，纯逻辑，有 doctest），
  `src/mod/ShopForm` 的下接框读取改成「下标（`uint64` / `double`）优先，否则按文案换算」，两种回传形式都认。

- `manifest.json` 补上 `dependencies: [{"name": "iListenAttentively"}]`（并写明 `"platform": "server"`）：
  以前只靠 `tooth.json` 声明依赖，模组目录里那一份没有，于是 LeviLamina 按目录名字母序先加载 `Shop`、
  后加载 `iListenAttentively`，`LoadLibrary(Shop.dll)` 解析导入表时找不到 `iListenAttentively.dll`，
  整个模组在真机上加载失败（`std::system_error` / `0x0000007E:system`）。依赖写进 manifest 后加载顺序由依赖图决定，
  与 LegacyScriptEngine 声明 `LegacyMoney` / `LegacyRemoteCall` 同一个机制。版本故意不写死（见 `DESIGN §6.4`）。
- `config.json` 缺 `version` 不再被当成“解析失败”：`ll::config::loadConfig` 返回 `false` 的语义是
  “已用默认值补齐/Rewrite”，不是读失败。以前手写一份只填几个键的配置会直接让模组拒绝启用（报“config.json 解析失败”），
  现在改为打 warn、用补齐后的配置继续，并把带 `version` 的完整文件回写一次。
- **「去两端空白」与「卖出价高于买入价怎么说」各收成一份实现**（`src/shop/Text.h`、`ShopSign::sellPriceAboveBuyMessage`）：
  牌面解析、份数表单、管理表单原本各有一份 `trim`，`S > B` 的规则抽出来后文案还是两份。两份判断的后果是
  同一段输入在一个入口能过、在另一个入口不能过，同一件事在两个入口有两种说法。
- **放弃「自己拼 NBT 包」这条老路**（DESIGN §9.2 已改）：真机上拼出来的包会让牌子显示空白、编辑界面打不开，
  客户端还会掉线。现在只让原版 `BlockActor::_getUpdatePacket` 出包，为空就先 `onChanged` 标脏再问一次，仍为空就放弃。
