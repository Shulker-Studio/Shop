# 04: 买一份

**What to build:** 选「购买」成交一份：钱扣掉、物品进背包、容器库存减少。这一条把记分板经济后端、交易服务、表单提交串成一条能看见的路径。失败路径（余额不足、库存不足、背包满）必须不留下任何半成品。

**Blocked by:** 03

**Status:** resolved

- [x] 成功购买后，余额、背包、库存三者数量一致
- [x] 余额不足 → 提示，三者都不变
- [x] 背包满 → 提示，三者都不变
- [x] 库存不足且允许部分成交 → 按实际可成交份数成交，总价按份数重算
- [x] 库存不足且不允许部分成交 → 整笔拒绝
- [x] 表单里显示的总价与最终扣款一致，且为整数
- [x] 交易失败时不留物品复制或金钱复制的窗口

## Answer

**已实现（`xmake` 构建通过，`xmake test` 111 cases / 2441 assertions 全绿）**

- `src/shop/Economy.{h,cpp}`：经济后端契约。`Account` 是 XUID 字符串；`TransferResult` 用 `Ok / Rejected / Broken` 代替 DESIGN §8.1 的「补偿失败抛异常」；`transfer` 默认实现 = `reduce` + `add`，`add` 失败就把钱加回去；`toScoreValue` 是 `int64 → int32` 的唯一一道范围检查（**拒绝，不截断**）。另有 `Backend` 枚举 + `parseBackend` / `backendName` / `backendNamesText`，配置里能写的后端名只有一张表。`NullEconomy`：读 0、写全成功、`available()` 为 false。
- `src/shop/TradePlan.{h,cpp}`：`planBuy` 纯逻辑决策，输入是一整个 `BuyRequest`（请求份数、每份件数、份价、余额、可成交份数、背包份数、`maxUnits` 天花板、`allowPartial`、店主是否在线、后端是否支持离线），输出 `BuyPlan{units, items, total}` 或一条拒绝原因（`OwnerOffline` / `NotEnoughStock` / `NotEnoughMoney` / `BackpackFull` / `InvalidRequest`）。份数上限由 `maxBuyUnits` 一处算出（`min(maxShopAmount, 可成交份数, 余额, 背包)`，DESIGN §10.1 第 3 步），表单与交易都调它；档位判定按「缺货 → 缺钱 → 背包」的优先级给出对得上号的原因。
- `src/mod/ScoreboardEconomy.{h,cpp}`：记分板后端。余额 = 玩家在 `economy.scoreboardObjective` 上的分值；`add`/`reduce` 都在 `int64` 里算好目标值，再用 `PlayerScoreSetFunction::Set` 写一次（不让引擎自己加减），写入前过 `toScoreValue`；账户解析失败（离线、无记分板身份、目标不存在）一律视为不可记账，返回 false 且不动分。
- `src/shop/BuyForm.{h,cpp}`：「买几份」表单的纯逻辑。下拉框 `1 / 2 / 4 / 自定义份数`（超过上限的档位不出现），正文逐档列出「总件数 / 总价」，`resolveRequestedUnits` 只接受 `1..maxUnits` 的整数（不悄悄夹到上限）。
- `src/mod/EconomyService.{h,cpp}`：后端选择与账户查询。**懒解析**：`enable()` 时世界往往还没起来，第一次真正要用钱时才按配置选，选出来就缓存。`auto` / `scoreboard` 走记分板，`null` 走空实现；`legacy_money` / `builtin` 在 `validate` 阶段就被**明确拒绝加载**（不是悄悄降级），选了它们说明服主想要那个后端记账。**记分板目标必须已经存在**（自动创建是 10 的 `createObjectiveIfMissing`），选不出可用后端时打错误日志（含手动建目标的命令）并降级为「交易全部失败」。
- `src/mod/TradeService.{h,cpp}`：`prepareBuy` 收齐「读店 → 读容器 → 读余额 → 算上限」的预校验（表单与交易共用同一套数，返回结构体而不是出参），`buyUnits` 是交易本体，顺序是**预校验 → 快照 → 先扣钱 → 先从商店箱取货、再塞进背包**（见下「为什么这个顺序不会漏」）；`requestBuyForm` / `submitBuy` 是表单入口，回调里不碰世界，把「谁的 XUID、哪个维度、哪块牌」投到 `ServerThreadExecutor`，执行时重新解析玩家与商店，提交侧加 250ms 冷却。补偿失败：`fatal` 日志 + 在线 OP 通知 + 把「玩家 + 商店」钉进内存冻结名单（`gFrozenTrades`），后续提交直接拒绝。`TradeResult` 内嵌 `BuyPlan`，并把「玩家看到的总价」（`BuySubmission::expectedTotal`）一起带进成交判定。
- `src/mod/ShopService.*` 拆成 `readShop`（只看世界）与 `inspectShop`（加看视角、读余额、定角色）；`ShopSnapshot` 带上 `ownerXuid`。`ShopInfoData::balance` 接真值，`currencySymbol` 只从 `economy.currencySymbol` 来（结构体里的 `"$"` 默认值删掉了）。
- `src/mod/ContainerFinderBds.*` 导出 `AttachedContainer`（含 `SignRef` 几何）、`mergedSlots`、`sameShopItem`、`firstProductStack`；`inspectAttachedContainer` 改为复用 `attachedContainerOf`，「合并槽位的顺序与边界」和「严格同种比较」各只剩一处定义。
- `Config.*`：新增 `allowPartialTransactions`（默认 `true`，DESIGN §10.3）、`ignoreCreativeMode`（默认 `true`，DESIGN §10.4）与 `economy` 块（`backend` / `scoreboardObjective` / `currencySymbol`）；`backend` 用 `parseBackend` 校验，错误文案里的可选值从那张表生成。
- `ShopInfoData` 新增 `unavailableReason`：店主收不了钱、或没有可用经济后端时，正文写「状态：<原因>」并且**不给买卖按钮**（DESIGN §8.5）。管理员店不受离线策略影响。
- `TradePricing::stockUnits` 改名 `unitsFromItems`：库存与背包容量共用同一个「件数 → 份数」算式。

### 为什么这个顺序不会漏

1. **先算完所有前置条件**（`planBuy`），任一条不成立就整笔返回，一分钱一件货都不动。
2. **先扣钱**：金额接口的失败语义最干净（`reduce` 返回 false 时余额一分不变），而物品搬运可能部分成功（DESIGN §10.2 第 5 步）。
3. **先从商店箱取货，再塞进背包**：中间那一刻商品只在一处（箱子已经少了、背包还没多）。反过来先塞背包再取箱子，中间那一刻就是凭空多出来的一份——这是唯一会产生复制窗口的顺序，所以不采用。
4. **每一步失败都有快照**：扣钱后任何一步失败，就按「整组槽位」的快照把商店箱（双箱两半）与背包写回，再反向 `transfer` 退款。用整组槽位而不是「本次动过的那几格」，是因为 `Container::addItem` 自己挑格子叠放，事先猜不到它碰了哪些槽位。
5. **补偿自己失败**（退款失败）返回 `broken`，打 `fatal` 日志并告诉玩家「请联系管理员」，不当普通失败处理（DESIGN §10.2 第 8 步）。

`ItemStack` 模板在动箱子之前**先拷一份**：`firstProductStack` 返回的指针指向箱子里的槽位，扣货之后那个引用就失效了。

### 决策与偏离

- **账户键用 XUID，不用 `mce::UUID`**（DESIGN §8.1 写的是 UUID）：纯逻辑层不得包含 `mc/` 头（`check_pure_logic_headers` 会拦），UUID 类型进不来；而且店主身份本来就是牌面记录的 XUID（ADR-0005），LegacyMoney 也以 XUID 记账（§8.7），用 UUID 反而要多一次反查。已在 `Economy.h` 里写明。
- **不实现 `getSymbol()`**：货币符号是商店的展示属性，只从 `economy.currencySymbol` 读（§8.7 第 6 条明确说各个后端拿不到自己的符号），后端再提供一个 getter 只会造出第二个来源。已在 `Economy.h` 里写明。
- **补偿失败不抛异常**：工程用 `set_exceptions("none")`，所以把「严重」做成返回值 `Broken`，由调用方打日志并告知玩家。
- **数量表单做成「逐档列总价」而不是「实时刷新」**（DESIGN §10.1 第 3 步）：`CustomForm` 的正文在发出时定死，要做到「选 2 份就立刻显示 20」需要 `sendUpdate` 反复重发整张表单，收益不抵复杂度。现在的做法是把每一档的总件数与总价都写进正文（`选 2 份 → 共 128 个 / $ 20`），选哪一档、看到的数就是实际扣的数，仍然满足「显示的总价 = 最终扣款，且为整数」。
- **「买一份」的默认路径还在**：不做数量表单直接买一份的语义没有丢——下拉框第一项就是 `1 份`，表单提交时请求份数照旧走 `planBuy`，部分成交的三种档位（库存 / 余额 / 背包）现在端到端可达。
- **管理员商店的交易整笔拒绝**（「管理员商店的交易还没实现」）：管理员店的资金端是系统账户，属于 11 的范围（DESIGN §7.4 / §8.2 都写明 P1），这里不伪造收款方。
- **不自动创建记分板目标**：`createObjectiveIfMissing` 是 10 的勾选项（「记分板目标名可配置，缺失时按配置决定是否创建」），04 只做「目标名可配置 + 目标存在就用」这一半；目标不存在时打错误日志（附 `/scoreboard objectives add <名字> dummy`）并降级为空实现，交易全部失败。
- **`auto` 目前等价于 `scoreboard`**：DESIGN §15 的默认配置就是 `"backend": "auto"`，所以不能拒它；10 才在 `auto` 前面插 LegacyMoney 探测，现在它只落到「记分板目标存在就用，否则降级为空实现并打错误日志」。`legacy_money` / `builtin` 相反：服主显式写了就说明他要那个后端，**配置校验直接拒绝加载**（错误信息写明「10 还没实现，请先写 auto 或 scoreboard」），不拿记分板顶替——否则余额会落到一个他没在看的地方。
- **犯不着为 `requireOwnerOnline` 加配置项**：§8.5 的表格对记分板后端就是「无条件拒绝离线店主」，这个 flag 现在改不了任何行为，留给 10。
- **提交冷却与点牌冷却分开两个 `TradeGate`**：共用一个窗口会把「点开表单后立刻按购买」误伤成静默忽略；两处都读同一个 `shopInteractionCooldownMs`，250 仍然只有一处定义。
- **表单回调一律切主线程**：`submitBuy` 只捕获 XUID / 维度 / 牌子坐标，`buyUnits` 在 `ServerThreadExecutor` 里跑，并用 XUID 重新解析玩家（排队期间可能已下线），维度变了直接放弃。03 的待验项 5（回调线程）因此不再阻塞 04。
- **玩家签约：硬编码不能与自己交易**（DESIGN §10.4 写的是「可配」）：§15 没有对应的配置键，先按不可配置实现。
- **创造模式禁止交易**：`ignoreCreativeMode`（DESIGN §15 的键，默认 `true`）在 `prepareBuy` 里拦，「可配」照 §10.4 原话实现；这条原先在 04–11 里没人认领，趁交易路径落地补上。
- **`planBuy` 的拒绝原因仍走纯逻辑层**，但「读不到店主」「管理员店」「后端不可用」这类需要世界状态的原因留在 `TradeService`，中文字面量不在两处重复。

### 已知边界

- **份数表单不会实时刷新**：正文里的总价是发出时算好的，玩家改下拉框不会让页面重算；预设档每一档的数都写着，自定义档给的是乘数与上限（玩家自己算），所以不会出现「显示 20 实际扣 30」。
- **`maxShopAmount` 一个键两种限制**：§5.1 用它限制「一份的件数」，§10.1 又把它算进「单次份数上限」。两者都是原设计写的，04 照做（`maxBuyUnits` 里取 min）；服主把它调到很小（比如 1）时，一次就只能买 1 份。已在 DESIGN §10.1 标注这个双关，键名拆分留到 10 之后。
- **确认判定比的是三个数**：份数、件数、总价。店主改了第 2 行的「一份的件数」时份数与金额可能都没变、只有件数变（看到 64 个只拿到 1 个），所以 `BuySubmission` 带 `expectedItems`；确认表单把重算后的三个数作为新的期望值再提交，**确认过也照比**——状态又变就再确认一轮，绝不静默按新值扣款。
- **两屏之间状态变了会先弹确认表单**：份数表单显示的是「弹出那一刻」的份数与总价，提交时 `buyUnits` 按最新状态重算；只要**份数**或**总价**与表单上看到的不一致（库存缩水、或者店主在这两屏之间改了价），它就不动任何钱货，返回 `needsConfirm`，由一张确认表单把真实份数、件数、总价摆出来（DESIGN §10.3「在确认表单里提前显示」），玩家点确认才成交。确认后仍会再算一次，若又变了就按实际成交并在消息里写明。
- **`ScoreboardEconomy::get` 对不可记账账户返回 0**（`Economy.h` 里写明了）：买入侧无害（买家必在线），但 05 的卖出路径要区分「店主资金不足」与「读不到店主余额」，届时得加一个能表达「读不到」的返回。
- **`inspectShop` 每次点牌都读一次余额**：记分板后端是一次 `getObjective` + 一次 `getPlayerByXuid` + 一次 `getPlayerScore`，点击级别的开销，没有缓存。
- **经济后端的选择在第一次用到钱时定下来**：补建记分板目标或改 `economy.backend` 之后要重启（与其它配置项一致，`/reload` 不重载配置）。
- **补偿失败会冻住这一对「玩家 + 商店」**：`fatal` 日志 + 在线 OP 通知 + 内存冻结键（`XUID@x,y,z`）。冻结在 `prepareBuy` 里拦，后续提交直接回「这笔交易已被冻结（上一次交易未完成，请联系管理员核对）」，不再重试；名单只在模组关闭/重启时清空——事故要人工核对，重启前一直有效是更安全的默认。05 的卖出路径可以直接复用。
- **免费店（`B 0`）允许 `total == 0`，跳过整笔转账**：`transfer` 的契约要求 `amount > 0`，所以 0 元单不产生任何账户操作。
- **背包容量按 `Player::getInventory()` 的空格 + 同种剩余叠加精确计算**：盔甲与副手是另外的容器、不参与叠放，所以这个数就是 `add` 真的能用的空间；万一 `add` 仍然失败（例如物品自身的叠放限制），会走「写回快照 + 退款」而不是把货丢在地上。
- **箱子侧的客户端刷新靠引擎**：`Container::removeItem` 内部走 `setContainerChanged`，`ChestBlockActor` 覆写了它去通知客户端；玩家背包额外补一次 `sendInventory(true)`。这条只能真机确认（已进 12）。

**审查后修正（code-review 两轴）**

- **`broken()` 现在打 `fatal`**（Standards 轴）：原来只有 `refund` 失败那条路径有严重日志，「付款转账返回 `Broken`」这条同样严重的路径零日志。现在两处都记。
- **两段补偿收尾合并成 `undoAndRefund`**（Standards 轴）：取货不足与背包塞不下两条路径的「写回快照 → 退款 → `broken`/`reject`」完全同形，分开写早晚有一边漏一步。
- **后端名收成枚举 + 一张表**（Standards 轴）：原来 `Config::isKnownBackend` 的数组、`EconomyService` 的字符串 if 链、错误日志文案各写了一遍后端名；现在 `parseBackend` / `backendName` / `backendNamesText` 共用 `kBackendNames`。
- **`attachedContainerOf` 带上 `SignRef`**（Standards 轴）：原来 `inspectAttachedContainer` 又要重读一次方块状态，还得兜底 `geometry ? ... : ref.pos`。
- **术语对齐 GLOSSARY**（Standards 轴）：注释与玩家文案里的「回滚」改成「补偿」，「库存」在新增代码里改成「箱子里的货」（`_Avoid_` 列表里有「回滚」「库存」）。
- **免费店不再谎称「钱已退回」**（Spec 轴）：`total == 0` 时根本没动过钱，收尾文案改成「这笔交易没有成交」。
- **店主收不了钱时表单不给买卖按钮**（Spec 轴，DESIGN §8.5）：`ShopInfoData::unavailableReason` 由 `inspectShop` 填（后来扩展成也覆盖「没有可用经济后端」），按钮列表与正文都据此收敛。
- **补了数量表单（复审）**：`requestBuyForm` 在服务端线程用 `prepareBuy` 算上限，再弹 `CustomForm`（`1/2/4/自定义`）；提交时请求份数才第一次变成真值，`buyUnits` 仍然按最新状态重算。表单值永远不是权威数据（DESIGN §10.4）。
- **撤掉了 `createObjectiveIfMissing`（复审）**：它是 10 的勾选项，04 越界了；现在只认已存在的目标。
- **补偿失败补了 OP 通知（复审）**：DESIGN §10.2 第 8 步的「严重日志 + 通知 OP」现在都在，冻结留给 05。
- **修掉头文件里失效的 `buyUnits` 声明（复审）**：`.h` 还留着旧的 `(int requestedUnits, bool confirmPartial)` 签名，`.cpp` 已经是 `BuySubmission`——声明无定义，05 照它调用就是链接错误。
- **确认表单的「确认」不再吃 250ms 提交冷却（复审）**：确认按钮紧接着第一次提交，再走一遍冷却会被静默吃掉，玩家点了没反应。
- **只改价时确认正文不说「只够成交几份」（复审）**：`buyConfirmContent` 现在收 `BuyPlan` + `unitsChanged`，份数没变只是价格变了时说「这家店的价格刚刚变了」，不再编一个假原因。
- **`TradeResult` 三个 bool 换成一个 `TradeStatus` 枚举（复审）**：`Done / Rejected / NeedsConfirm / Broken` 互斥，构造不出「既成功又坏了」。
- **「不能交易」的两句原因收进 `ShopInfo`（复审）**：`ownerOfflineReason()` / `noEconomyReason()`，表单（`inspectShop` 的 `unavailableReason`）、`prepareBuy`、`buyRejectionMessage` 三处都调同一个函数，不再四种说法。
- **管理员店不再被「店主不在线」挡住（复审）**：§8.5 的表格说管理员店不受离线策略影响，`inspectShop` 现在跳过这条判断（`info.adminShop` 为真时不填 `unavailableReason`），管理员店照常给按钮、点击后得到「管理员商店的交易还没实现」。
- **自定义份数档写出真实乘数与上限（复审）**：「总价 = $ 10 × 份数（1 到 4 份）」——自定义档没法实时算，至少让玩家自己算得出来。
- **确认正文按原因分三种说法（复审）**：`ChangeReason::Units / Items / Price`，并总是把份数、件数、扣款三行都写出来（只改份量时不再谎称「价格变了」）。
- **`appendLine` 提到 `ShopInfo` 共享（复审）**：`shopFormContent` 与两张买表单不再各拼一遍换行。
- **补了部分成交/改价的确认表单（复审）**：`buyUnits` 在「实际份数或总价与玩家在份数表单上看到的不一致」且未确认时不动钱货，返回 `needsConfirm` + 重算后的 `BuyPlan`，由 `openBuyConfirmForm` 摆给玩家；确认后才执行。只比份数会漏掉「份数没变但店主改了价」这一路（看到 `$40` 扣 `$80`），所以 `BuySubmission` 把 `expectedTotal` 一起带进来比对。
- **回写背包后也刷客户端（复审）**：`undoAndRefund` 恢复了背包却只刷了箱子，客户端会留着已经退回去的物品；现在补 `sendInventory(true)`。
- **「不能交易」的原因做成一个字段（复审）**：原来只有「店主不在线」，没有经济后端时照样给买卖按钮；现在 `ShopInfoData::unavailableReason` 同时覆盖两种原因，正文与按钮都据它收敛。
- **`EconomyService` 里那段「未实现后端按 auto 处理」的死分支删掉了（复审）**：配置校验已经硬拒，代码里不该留着相反的策略。
- **`prepareBuy` 改为返回结构体（复审）**：与 `RequestedUnits` / `BuyDecision` 的惯例一致，不再用出参。
- **两个「份数」标签分开了（复审）**：买表单里原来「可成交份数」与「这次最多买 N 份」两个数并排、标签相近；现在前者叫「箱子里的货还够」，后者叫「这次最多买（受余额、背包与单次上限限制）」。
- **`BuyContext::maxUnits` 改名 `affordable`（复审）**：它与 `request.maxUnits`（配置天花板）同名不同义，并排出现容易取错。
- **`BuyForm` 的两个同形 `append` 闭包抽成 `appendLine`（复审）**；`parseUnits` 里不可能命中的溢出检查删掉（前面已经限了 9 位）。
- **`TradeResult` 内嵌 `BuyPlan`（复审）**：原来 `units/items/total` 三个字段与 `BuyPlan` 同义重复。
- **DESIGN §8.4 补了 P0 阶段性说明（复审）**：`legacy_money` / `builtin` 在 04 是「配置错误、拒绝加载」，`auto` 等价于「记分板目标存在就用」。文档与实现对齐，免得后人照 §8.4 配了就起不来。
- **补了冻结（复审）**：补偿失败时把「玩家 + 商店」钉进内存名单，后续提交拒绝（DESIGN §10.2 第 8 步）。
- **补了容器刷新（复审）**：`BlockActor::refresh(region)` 是 DESIGN §7.3 说的那个 refresh；现在成交后对商店箱**两半各刷一次**（`refreshShopBox`），补偿写回后也刷，不再只靠 `removeItem` 内部的 `setContainerChanged`。
- **`maxShopAmount` 超限给了独立拒绝原因（复审）**：`TradeRejection::OverUnitLimit`，提示「一次买的份数超过了这家店的单次上限」，不再落到「份数或价格不合法」。
- **`legacy_money` / `builtin` 改成显式拒绝加载（复审）**：原来打警告后按 `auto` 处理，等于偷偷换后端；现在配置校验直接失败并提示「10 还没实现，请先写 auto 或 scoreboard」。`auto` 保留（§15 的默认值），它就是「目标存在就用记分板，否则降级」。DESIGN §8.4 已同步这条阶段性说明。
- **补了份数上限 `maxShopAmount`（复审）**：DESIGN §10.1 第 3 步把 `maxShopAmount` 列在上限里，原来只算了库存/余额/背包；现在 `maxBuyUnits` 一处算，表单与 `planBuy` 共用。
- **补了 `ignoreCreativeMode`（复审）**：DESIGN §10.4 / §15 有这个规则和键，原来四个 ticket 都没认领。
- **退款只记一条日志（复审）**：`refund` 不再自己 `fatal`，失败细节交给 `broken()` 统一记一次并通知 OP（原来同一次事故打两条措辞不同的 fatal）。
- **术语对齐（复审）**：新增代码里的「库存」「钱包」「数量表单」全部换掉（GLOSSARY 的 `_Avoid_` 列表），改成「商店箱里的货 / 可成交份数 / 余额 / 份数表单」。
- **`ShopForm` 的按钮动作收成一个 switch（复审）**：原来 `Buy` 在 `notifyPending` 里有一条死分支。
- **`refund`/`undoAndRefund` 的 `(买家, 店主, 金额)` 打包成 `Payment`（复审）**：这三样在所有收尾路径里结伴出现。
- **`EconomyConfig::backend` 认不出时不再静默当 `auto`（复审）**：改成打错误日志且不提供后端。
- **未采纳**：对 `Config.h` / `ContainerFinderBds.*` 跑 clang-format（仓库既有文件本来就不是 format-clean；这两个文件跟着本地对齐写，重新格式化只会把 03 的代码搅一遍）；`undoAndRefund` 与 `buyConfirmContent` 的 bool/枚举入参（两个分支都是「同一件事的两种收尾」，拆函数反而多两处调用点）；把 `TradeService.cpp`（~500 行）拆成多个模块（快照、容量、OP 通知都是只被本文件用的内件，拆开只是换个地方漂）；`reject()` / `broken()` 改名（它们产出的 `TradeResult` 字段就叫这两个词）；`Shop::getInstance().getSelf().getLogger()` 的短别名（03 的旧代码也是这么写的，只给新文件加会立刻不一致）；`(BlockSource&, sign, ShopConfig const&)` 打包成一个类型（读者只有三处，包起来是新的间接层）；`EconomyService` 里的 `onlinePlayer` 拆出去（它和「谁的钱」是同一件事，已在头注释里说明这个模块管两件事）。
- **`backpackUnits` 的注释改对了**（Spec 轴）：原来写「故意往多了算（盔甲、副手也算）」，实际上 `Player::getInventory()` 就只含背包与快捷栏，盔甲/副手是另外的容器，所以这个容量是精确值、不是高估。

**只能真机验收的项（留给 12）**

1. 买一次后余额、背包件数、商店箱件数三者一致（§13 第 21 条）。
2. 份数表单：下拉框只出现上限以内的档位、「自定义份数」填超上限会被拒（不静默夹到上限），每档显示的总价与实际扣款一致。
3. 创造模式玩家点商店牌 → 提示「创造模式不能与商店交易」，三者都不变；把 `ignoreCreativeMode` 改成 `false` 后可以正常买。
2. 余额不足 / 背包满 → 提示且三者都不变（§13 第 22、24 条）。
3. 记分板后端真的能读能写：`/scoreboard players list` 里的分值随交易变化，重启后仍在。
4. 表单回调线程：现在一律切到 `ServerThreadExecutor` 再动世界，但「回调本来就在主线程」这件事仍未确认——只是不再影响正确性。
5. 商店箱 UI 开着时（店主）能否看到数量变化（`setContainerChanged` 是否真的把更新推给客户端）。
6. `Player::getInventory()` 的 `add` 与我们的容量计算是否一致（装满到最后一格再买一次，看会不会走「写回 + 退款」）。
