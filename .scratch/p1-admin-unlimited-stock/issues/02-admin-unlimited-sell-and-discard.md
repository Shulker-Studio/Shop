# 02: 管理员店卖出无限 + 销毁留档

**What to build:** 玩家卖货给管理员店永远成功，不会因为"商店箱满了"被拒；交出去的货**销毁**（不写进箱子，样品槽一件不动），同时把被销毁的物品完整 NBT 交给留档通道。

**Blocked by:** 01

**Status:** resolved

- [x] 卖出方向的份数上限里**去掉"商店箱空间"这一项**：货是销毁的，永远收得下；其余上限（玩家手里的货、店主余额、`maxShopAmount`）照旧生效
- [x] `ShopInfoData::boxRoomUnits` 也得能表达"不限制"（现在是个 `int`，用 01 引入的 `StockView` 或 `optional<int>` 都行），
      否则表单会显示「商店箱还能装 0 份」并把上限夹成 0
- [x] 卖出执行路径：给钱 → 从背包取货 → 销毁（不写容器）→ 完成；样品槽与其余格子都保持原样
- [x] 销毁前把「被销毁的物品堆」交给流水的 `discarded` 通道。**取 SNBT 的正确写法**：`stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`（`ItemStack` 自己没有 `save(CompoundTag&)`）；留档在 `Done` 之前生成、`Done` 之后落盘，留档失败只 `warn`，**不许**因此把交易判失败
- [x] 补偿必须对称：钱记上了但取货失败 → 把已取走的物品还给玩家；任一补偿失败 → 与 P0 同一套（`fatal` + 通知 OP + 冻结这一对）
- [x] 表单正文别写「商店箱还能装 0 份」这类误导数字
- [x] 单测：无限收购下箱空间不参与上限；玩家手里没有货仍被拒（`NotEnoughGoods`）；店主余额不足仍被拒；普通店卖出行为不变
- [ ] 真机（**未验**）：卖 10 次 → 每次都收下并给钱、箱子一件不动（第一格逐字段不变）、`logs/discarded-*.jsonl` 里能 `jq` 出对应的 `item.nbt`
- [x] `xmake test` 全绿，测试数与断言数写进本票

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- `ShopInfoData::boxRoomUnits` 从 `int` 换成 `StockView`（与票 01 的 `stockUnits` 同一个类型）：
  管理员店 `makeUnlimited()`，普通店照旧准确数字。份数上限里那一项因此**不进 `min`**，
  不会再有「表单说还能装 0 份、上限被夹成 0」的第二种真相。
- 新增 `stockUnitsText` 的姊妹 `boxRoomText`：有限 `7 份`，无限 `无限（管理员店，永远收得下）`。
  两者共用一个 `countUnitsText` 实现，只有无限时那句括号不同（**有货** / **收得下**是两件事）。
- `UnitsForm` 卖出侧：箱空间那行改走 `boxRoomText`；管理员店的上限来源从
  「受你手里的货、商店箱空间与单次上限限制」改成「受你手里的货与单次上限限制」——
  不再把一项不存在的限制摆给玩家看。
- 新增 `DiscardRecord`（`src/shop/DiscardRecord.h/.cpp`）：一行一个**被销毁的物品堆**的 NDJSON，
  键序固定 `ts` / `shop{dim,x,y,z}` / `player{xuid,name}` / `item{id,aux,name,nbt}`。
  字符串转义只做 JSON 要求的那几个（引号、反斜杠、控制字符），UTF-8 原样写——
  转成 `钻石` 之后文件里就找不到「钻石」了。文件名 `discarded-YYYY-MM-DD.jsonl` 取 `ts` 前 10 位，
  两者因此永远同一天。

**BDS 适配（`src/mod`）**

- `TradeService::takeMatchingItems` 多一个可选出参 `taken`：买入方向不传（货发给玩家，不销毁），
  管理员店卖出方向传——`removeItem` 之后物品就没了，**这是唯一能拿到那堆东西的时刻**，
  所以在扣货的同一处顺手复制一份（`copy.set(take)`，只留被扣走的那部分）。
- `sellUnits`：管理员店跳过 `putItemsIntoShopBox`（货是销毁的，永远「收得下」），
  也不再 `refreshShopBox`（箱子一个字没动）。所有步骤成功后把 `destroyed` 描述成记录塞进
  `TradeResult::discarded`，然后 `destroyed.clear()`——销毁之后没有任何地方还持有这些物品。
- `finishTrade`（唯一的终态漏斗）在 `result.done()` 之后调 `writeDiscardRecords`：
  **记录在 `Done` 之前生成、落盘在 `Done` 之后**，写失败只 `warn`，绝不回头把成交判成失败。
- `prepareSell`：管理员店 `boxRoomUnits = makeUnlimited()`，普通店仍调 `shopBoxUnits`。
- 新增 `src/mod/WallClock.*`（本地 ISO-8601 带时区偏移；偏移用「把本地墙上时间当 UTC 再解释一次」
  的差算出，不依赖 `tm_gmtoff` 与 `strftime("%z")` 的平台差异）与 `src/mod/DiscardLog.*`
  （`describeDiscardedItems` 生成 SNBT、`writeDiscardRecords` 追加写；`ios::binary` 保证 `
` 不被翻成 `

`）。
  SNBT 取法按票面：`stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`。
- 补偿对称（票面要求，路径本来就对称，这次只是把「写进箱子」那一步换成「销毁」）：
  钱记上了但取货失败 → `undoAndRefund(restoreBackpack=true)` 把背包按快照写回并退款；
  退款也失败 → `broken()` 同时做三件事（`fatal` 日志 + 通知在线 OP + 冻结这一对「玩家 + 商店」）。

**文档**：DESIGN `§7.4` 补「票 02：卖出方向无限、收进来的货销毁 + 留档」五条；`§10.1` 补「P1 现状（票 02 落地后）」；
`§10.3` 的两条公式注明卖出侧的「容器剩余空间」在管理员店为无限且货是销毁的；`GLOSSARY.md` 新增「销毁留档」
（含 `_Avoid_`：流水 / 备份）；`CHANGELOG.md` 记一条。

**没做的一件（有理由，也是已知缺口）**：`p1-transaction-log` 的 D4 / D5 还要求「按天轮转 + 7 天后 gzip」
与「`transactionLog.enabled = false` 时两个文件一起停」。这两条都**没有**落地：本票没有自己发明
`transactionLog` 配置块（那会与该 spec 的字段定义打架），所以现在这条通道**无条件**追加写、不压缩，
文件按天分（`discarded-YYYY-MM-DD.jsonl`）但绝不删除。日常体量是「管理员店收到的货」，
真到了需要压缩的那天，`p1-transaction-log` 落地时会接管开关与轮转——那是它的地盘，
票面也只要求「通过，不包括这部分。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**288 cases / 3356 assertions，全绿**
  （票 01 后 278 / 3332 → **+10 cases / +24 assertions**；基线 260 / 3254）
- 分布：新增 `DiscardRecordTest`（5 个：键序固定的完整字符串比对、转义后仍是一行、中文原样、nbt 原样透传、文件名）；
  `TradePlanTest` +4（无限收购下箱空间不进上限而手里的货/店主余额照旧、没有货仍报 `NotEnoughGoods`、
  关闭部分成交不会被报成「装不下」、`maxTradeUnits` 里那一项不存在）；
  `UnitsFormTest` +1（箱空间写「无限」、上限来源不再列箱空间）。
- **普通店行为一个字没改**：`TradePlanTest` / `TradePricingTest` 的旧断言一条未动。
  唯一改动的既有断言在 `UnitsFormTest`（管理员店卖出的上限来源那句，去掉了已不存在的「商店箱空间」），
  这条断言描述的正是本票要改掉的那个上限，不是普通店行为。

### 真机项（**未验**）

下面一条都没有在 BDS 上跑过，只有纯逻辑测试。逐条写清退路，**不许当成已验**：

1. **卖 10 次 → 每次都收下并给钱、箱子一件不动（第一格逐字段不变）** —— **未验**。
   退路：结构上管理员店卖出路径已经不再调用 `putItemsIntoShopBox`（`sellUnits` 里的 `info.adminShop` 分支），
   若真机发现样品槽被改，只可能来自 `TradeSnapshots.restoreShopBox` 的补偿路径——
   那说明某一步失败了，先看 `trade_result` 的 `outcome`/`reason`。
2. **`logs/discarded-*.jsonl` 里能 `jq` 出对应的 `item.nbt`** —— **未验**。单测只覆盖序列化，
   文件 IO 按 spec 的 Testing Decisions 不进单测。退路：
   - 文件没生成 → 先看目录（`plugins/Shop/logs/`）与 warn 日志（`create_directories` 失败或文件打不开都会留一句）；
   - 有文件但没有记录 → 说明 `TradeResult::discarded` 没被填（`describeDiscardedItems` 的入参 `destroyed`
     是否为空）；`jq -c . discarded-*.jsonl` 能逐行验证合法性。
3. **`stack.save(SaveContext{Clone})->toString()` 真机给出的 SNBT 是完整 NBT**（附魔、耐久、自定义名都在）—— **未验**。
   退路：先用 `/give` 造一把带附魔的工具卖进去，`jq '.item.nbt'` 看有没有 `ench`；若 `SaveUseCase::Clone`
   在某些栈上给的是精简版，退路是换 `SaveUseCase::SaveToDisk`（仍然没有 `save(CompoundTag&)` 这个重载）。
4. **`nowIso8601Local()` 的时区偏移与服务器本地时间一致（含夏令时）** —— **未验**。
   退路：用 `_mkgmtime` 的差算偏移，若发现 `+08:00` 变成 `+00:00`，说明 `localtime_s` 没拿到本地时区
   （容器/服务端时区配置），改读 `TIME_ZONE_INFORMATION` 或退回 `strftime("%z")` 后补冒号。
5. **磁盘满 / 文件被占用时只 warn、交易仍成功** —— **未验**。
   退路：`writeDiscardRecords` 的三个失败点都只 `logger.warn` 并 `return`，没有任何返回值参与 `TradeResult`；
   真机可以临时把 `logs` 建成同名**文件**来模拟目录创建失败。

### 代码审查后的修改（Standards / Spec 两轴并行审 `git diff a737762...HEAD`）

**改掉的**

- Spec 轴：票面真机项退回 `- [ ]` 并标「未验」（勾着「卖 10 次已过」而 Answer 说没跑过是自相矛盾）。
- Spec 轴：`DiscardRecordTest` 的用例数写错了（写 4 个，实为 5 个），已改；总数 288 / 3356 当时是对的。
- Spec 轴：把 `p1-transaction-log` D4 / D5 的两条要求（轮转 + 压缩、`transactionLog.enabled` 一起停）
  从「没做的一件」升级成**明确列出的已知缺口**：文件按天分但只增不减、开关还没地方关。
  这是与该 spec 的边界（它定义配置块与轮转规则，本票只负责「交出物品堆」），不是漏做。
- Standards 轴：管理员店卖出方向的「部分成交」文案把「单次上限」列进原因（原文案断言了它不知道的原因）。

**留着的（有理由）**

- 见票 01 的那三条（`prepareCommon` 的空箱分支、`StockView::valid()`、`info.adminShop` 的多处分叉）。
- `describeDiscardedItems` / `writeDiscardRecords` 分成「造记录」与「落盘」两个函数：
  这不是 Middle Man——两件事发生在**不同的时刻**（`Done` 之前 / 之后），合成一个函数就没法表达票面那条顺序要求。
