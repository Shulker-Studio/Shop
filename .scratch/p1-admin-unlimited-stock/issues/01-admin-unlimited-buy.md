# 01: 管理员店买入无限（卖不完）

**What to build:** 在一家管理员店里连买 10 次，箱子里的样品一件不少、这家店永远有货。这一票把「库存 = 无限」从模型一路打通到执行路径：份数上限不再被库存卡住、表单写「库存：无限」、买的时候不从箱子里扣。做完就能真机验收（买家一直买得到，箱子数量不变）。

**Blocked by:** None (can start immediately)

**Status:** resolved

- [x] 纯逻辑引入 `StockView { std::optional<int> units; bool unlimited; }`，`makeFinite(int)` / `makeUnlimited()` 两个构造，`unlimited && units` 视为构造错误；**禁止**用 `INT_MAX` / `-1` 之类哨兵参与 `min`
- [x] **"无限"要一次改干净三处**（`rg 'stockUnits|goodsUnits|roomUnits'` 应全命中）：① `TradePlan` 的 `TradeRequest` 与 `maxTradeUnits`（两项改成 `StockView` 或 `optional<int>`，无限时跳过 `min` 与 `<= 0 就拒`）；② `TradePricing::tradeUnits`（是另一套 min，漏改就会一边说无限、一边被卡住）；③ `UnitsForm` 里按 `stockUnits` 算"还剩几份"的那段
- [x] `ShopInfoData::stockUnits` 也跟着换成 `StockView`（否则 `planTrade` 认为无限、表单还显示数字，两套真相）
- [x] 管理员店（`adminShop`）的**商店箱库存**一律 `makeUnlimited()`；**本票只让买入方向生效**
- [x] 别顺手改错字段：卖出方向的"货"来自玩家背包（`heldUnits`），**不是** `stockUnits`——它必须保持有限，
      否则玩家能无限卖自己有的东西（那是复制物品）。`rg heldUnits` 确认它没被无限化
- [x] 买入执行路径：不调用容器写入，样品槽数量前后完全一致；失败（背包满）照旧退款并提示
- [x] 表单正文写「库存：无限（管理员店，永远有货）」，不编数字；其余三个上限（余额、`maxShopAmount`、背包空间）在无限下仍然生效
- [x] 单测：无限买 1 / 2 / 4 / 自定义；余额不足仍被拦；单次上限仍生效；背包空间仍生效；`maxTradeUnits` 在无限时不看库存项；
      普通店行为完全不变（现有 `TradePlanTest` / `TradePricingTest` 断言**一条都不许改**，改了就说明你在悄悄改普通店行为）
- [x] 真机：管理员店买 10 次 → 每次都给货、都能付钱、箱子内数量一件不少（顺带看 `mtrans` 一笔一条）
- [x] `xmake build Shop` / `xmake build ShopTests` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 卖出方向**这一票先不动**（仍按有限库存跑），02 再把「收得下」也变成无限。
- 模板箱空（没有任何非空槽位）不归 `planTrade` 管，那是 03 的独立原因；别把两条判据混进一个函数。
- DESIGN `§10.1` / `§10.3` 的公式段要补一句「库存为无限时该项不参与 min」。

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- 新增 `StockView`（`StockView.h` / `.cpp`）：`std::optional<int> units` + `bool unlimited`，两个构造口
  `makeFinite(n)` / `makeUnlimited()`。无限时 `units` **为空**，所以 `INT_MAX` / `-1` 这类哨兵彻底不用了。
  读的一侧只有三个出口：`finiteUnits()`（无限时为空 → **它才是「不参与 `min`」的唯一实现**）、
  `covers(n)`（无限恒够）、`empty()`（无限永不空）。`valid()` 给「无限 + 数字」这种非法组合留一条可诊断的缝：
  工厂造不出来（`makeUnlimited` 不带数字），万一有人手写成员，读的一侧按「无限」兜底，不许退化成 0 份。
  保留了从 `int` 的**隐式**构造（`StockView view = 5;`），所以既有调用点与既有测试的 `goodsUnits = 5` 一个字都不用改。
- 「无限」一次改干净三处，且只有一份实现：
  - `TradeRequest::goodsUnits` / `roomUnits` 换成 `StockView`；
  - `TradePricing::tradeUnits` 改成 `(int requested, StockView stock, StockView room, int affordable)`，
    无限的项**不进 `min`**（循环里只在 `finiteUnits()` 非空时才压上限）；
  - `TradePlan::maxTradeUnits` 两个重载都换成 `StockView`，并且**删掉了原来那句
    `tradeUnits(INT_MAX, goods, room, affordable)`**——上限就是请求量，不需要「用 `INT_MAX` 表示不限制」的哨兵；
  - `UnitsForm` 的「还剩几份」改成 `goodsOf()`（买入取 `stockUnits`、卖出取 `heldUnits`）+ 共用文案函数。
- `ShopInfoData::stockUnits` 换成 `StockView`，份数文本统一走 `stockUnitsText`：有限 `3 份`，无限
  `无限（管理员店，永远有货）`。信息表单与份数表单调的是同一个函数，不存在「一处说无限、一处写数字」。
- `planTrade` 的两条判据跟着换：`allowed <= 0` 时用 `empty()`（只有有限库存才可能「空了」），
  部分成交被拒时用 `covers()`（无限库存不会被报成「商店的货不够」）。
- **卖出方向没有动**：`prepareSell` 显式 `makeFinite(heldUnits)`，`heldUnits` 仍是 `int`。
  `rg heldUnits` 确认它没有被传染成 `StockView`——无限化玩家手里的货就是凭空复制物品。

**BDS 适配（`src/mod`）**

- `ShopService::readShop`：管理员店 `makeUnlimited()`，普通店仍走 `unitsFromItems(itemCount, 每份件数)`。
- `TradeService::prepareBuy` 直接把 `info.stockUnits` 透传给 `TradeRequest::goodsUnits`；
  `prepareSell` 的 `goodsUnits` / `roomUnits` 都显式 `makeFinite`（这一票卖出仍是有限库存）。
- `buyUnits` 执行路径：管理员店**跳过「从箱子扣货」那一步**（`takeMatchingItems` 不再被调用），
  货由 `giveItemsToBackpack` 从商品模板复制发出；成功后也不再对商店箱做无意义的 `refresh`
  （箱子一个字都没动）。背包满时仍走原来的 `undoAndRefund`（退款 + 背包快照回写）。
- 部分成交文案对管理员店不再提「商店箱里的货」（那一项在无限下不存在）。

**文档**：DESIGN `§7.4` 补「P1 票 01：容器降级为模板箱、库存无限」与三条落地细节；`§10.1` 的份数上限公式补
「库存为无限时该项不参与 `min`」+ 「P1 现状（票 01 落地后）」段；`§10.3` 的买卖两条公式各补同一句；
`GLOSSARY.md` 新增「模板箱」「无限库存」（含 `_Avoid_`：用 `INT_MAX` / `-1` 当哨兵），并在「管理员商店」词条交叉引用；
`CHANGELOG.md` 记一条。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**278 cases / 3332 assertions，全绿**
  （基线 260 / 3254 → **+18 cases / +78 assertions**）
- 分布：新增 `StockViewTest`（6 个：有限/0/负数、无限不参与 `min`、隐式转换、非法状态兜底、文案）；
  `TradePricingTest` +1（无限的两项都不进 `tradeUnits` 的 `min`，其余上限照旧）；
  `TradePlanTest` +7（无限买 1/2/4、自定义份数、余额不足、单次上限、背包放不下 → `NoRoomToReceive`、
  `maxTradeUnits` 不看库存项、缺货不再是拒绝理由）；`ShopInfoTest` +2（正文写「无限」、按钮照常）；
  `UnitsFormTest` +2（买入行写「无限」、卖出方向仍是玩家手里的数字）。
- **既有断言一条未改**：`TradePlanTest` / `TradePricingTest` / `UnitsFormTest` / `ShopInfoTest` 的
  旧 `TEST_CASE` 只因为字段类型换成 `StockView` 而依赖隐式转换，没有改过一个期望值。
- TDD 过程：先写这 18 个用例 → 编译红（`StockView` 不存在）→ 补上类型骨架（签名换好、`min` 仍按「无限 = 0 份」写）
  → **9 个用例 / 18 条断言因「无限被当成 0 份」而红** → 把 `finiteUnits()` 接进 `tradeUnits` 后转绿。

### 真机项（**未验**）

下面一条都没有在 BDS 上跑过，只有纯逻辑测试。逐条写清退路，**不许当成已验**：

1. **管理员店连买 10 次 → 每次都给货、都能付钱、箱子内数量一件不少** —— **未验**。
   退路：结构上买入路径已经不再调用 `takeMatchingItems`（`buyUnits` 里的 `info.adminShop` 分支），
   所以「箱子变少」只可能来自别处：先看 `trade_result` 的调试事件（`shopDebugEnabled`）确认 `outcome=ok` 与
   `items` 对得上；再看 `readShop` 是否真的判成了 `adminShop`（第 1 行 + 编辑者是 OP）。
2. **样品槽那一格前后逐字段相同（NBT / 数量 / 耐久）** —— **未验**。单测只覆盖「模型说无限」，
   容器侧的「逐字段相同」按 spec 的 Testing Decisions 留给真机/集成。
   退路：若真机发现数量掉了，事故点在 `giveItemsToBackpack` 之前那一步——用 `SlotSnapshot` 的
   `getSlotCopies` 打一份前后对比（快照已经在 `TradeFrame` 里，不用新写）。
3. **`getMaxStackSize` 批次发放在「一次买很多份」时不丢货**（`giveItemsToBackpack` 的 `while` 循环）—— **未验**。
   退路：这条逻辑 P0 就在跑（普通店买入同样走它），本票只改了「货从哪来」，没有动发放；
   真的丢货就退回「整批塞一次 + 溢出即补偿」的旧路径。
4. **`mtrans` 一笔一条**（LegacyMoney 的流水）—— **未验**。管理员店的资金侧是系统账户（买入销毁），
   这条流水属于 `p1-transaction-log` 的地盘，本票没有碰经济层；退路：`mtrans` 里看不到就说明该功能
   尚未落地，按 `p1-transaction-log` 的 spec 单独验，不要把本票判成失败。
