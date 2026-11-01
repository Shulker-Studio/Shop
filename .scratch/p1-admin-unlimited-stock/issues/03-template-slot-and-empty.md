# 03: 样品槽语义与「还没设商品」

**What to build:** 管理员店的那个箱子明确变成"模板箱"：**第一个非空槽位就是样品槽**——买入不从它扣、卖出不往它放、交易前后逐字段相同；第一格以外的格子一律忽略；整个箱子空着时，这家店说「还没设商品」而不是「卖光了」。

**Blocked by:** 02

**Status:** resolved

- [x] 样品槽 = **第一个非空槽位**（每次现算，不是固定的 0 号槽）：0 号空、1 号放钻石时，钻石就是样品
- [ ] 交易前后样品槽逐字段不变（**未验**：spec 的 Testing Decisions 明确「不在单测里造容器」，所以这条没有 `ItemStack` 相等的单测，只有真机项）
- [x] 第一格以外的格子**一律忽略**：不参与库存与空间计算，不构成拒绝理由（往第二格丢几组东西，买卖照旧）
- [x] 模板箱空 → 新增一条不可交易原因：「这家管理员店还没设商品：往商店箱里放一件样品，它就卖这个」。它与普通店「商店箱里没有货」**用不同的判据与文案**，不许合并
- [x] 点牌表单：模板箱空 → 不给买卖按钮；有样品 → 照常给。OP 的管理入口不受影响（能进去补样品）
- [x] 换商品 = 换第一格的东西，不做任何持久化也不需要清空整箱；同一笔交易里商品只读一次
- [x] 单测：样品槽判定（0 号空/1 号有）、其余格忽略、空箱原因与普通店原因分开、按钮集合在这两种状态下不同
- [ ] 真机（**未验**）：清空模板箱 → 点牌看到「还没设商品」且无按钮 → OP 放一件样品 → 立刻能买；第二格塞几组泥土 → 商品与份数都不受影响
- [x] 文档：`DESIGN §7.4` 写清「模板箱不参与库存增减、卖进来的物品销毁（留档）」；`GLOSSARY.md` 加「样品槽」
- [x] `xmake test` 全绿，测试数与断言数写进本票

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- 新增 `SampleSlot.h`：`sampleSlotIndex(slots, isEmpty)` —— **合并槽位里第一个非空的那一格**。
  它是一个模板函数而不是写死 `ItemStack`：`src/mod` 只提供「第 i 格空不空」，于是
  「0 号空、1 号有钻石」「0 号有样品、2/3/40 号塞了泥土」「整箱空」这三种情形都能在单测里直接断言，
  不必造容器（spec 的 Testing Decisions 正是这么切缝的）。
- 新增 `unavailableReasonForEmptyBox(bool adminShop)`：两条原因**分开**，不合并——
  - 普通店：`emptyBoxReason()`「商店箱里没有货，现在没法交易（补货后恢复）」；
  - 管理员店：「这家管理员店还没设商品：往商店箱里放一件样品，它就卖这个」。
  判据是同一句「第一个非空槽位不存在」，但语义完全不同：前者等补货（玩家能等到的），
  后者只能由 OP 往模板箱里放样品。把这句写成「缺货」会让玩家以为过一会儿就自动有了。
- `shopFormButtons` **一个字没改**：它本来就按 `unavailableReason` 藏买卖按钮、按 `canManage` 给管理入口，
  所以「模板箱空 → 没有买卖按钮、管理入口照给」是既有结构的自然结果，本票只是补上断言。

**BDS 适配（`src/mod`）**

- `ContainerFinderBds::firstNonEmptyStack` 改成调用 `sampleSlotIndex`：规则不再在 mod 层重写，
  它只回答「这一格空不空」。`firstProductStack`（交易用）与 `inspectAttachedContainer`（开店/信息用）
  共用它，所以「样品槽」和「商品模板」永远是同一格。
- `ShopService::readShop`：箱子空时按店的性质填 `unavailableReason`（`unavailableReasonForEmptyBox(info.adminShop)`）。
- `TradeService::prepareCommon`：`firstProductStack == nullptr` 时，管理员店返回「还没设商品」，
  普通店保持原来那两句（买入 `NotEnoughGoods` 的「商店的货不够」、卖出「商店箱里没有货，看不出这家店收什么」）。
- 其余格忽略与「不做持久化」是结构性成立的，本票没有新增代码：管理员店的库存与箱空间都是
  `StockView::makeUnlimited()`（票 01 / 02），`itemCount`（跨所有格统计严格同种件数）只喂普通店的
  `stockUnits`；商品每次 `readShop` 现读，`prepareCommon` 只读一次并拷成 `context.product` 副本，
  之后每一步都读副本。

**文档**：DESIGN `§7.4` 新增「P1 票 03：那个箱子明确变成模板箱」五条（不参与库存增减 / 卖进来的销毁留档 /
其余格忽略 / 空箱 = 还没设商品 + 两条原因分开 / 不做持久化）+ `§10.1` 的「P1 现状（票 03 落地后）」；
`§5.3` 注明「第一个非空槽位」在管理员店叫样品槽、且「第一个」不是 0 号槽；`GLOSSARY.md` 新增「样品槽」
（含 `_Avoid_`：第一格 / 商品槽 / 库存箱）；`CHANGELOG.md` 记一条。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**296 cases / 3382 assertions，全绿**
  （票 02 后 288 / 3356 → **+8 cases / +26 assertions**；基线 260 / 3254）
- 分布：新增 `SampleSlotTest`（4 个：0 号有 → 0；0 号空 / 1 号有 → 1；前面空得更多 → 更靠后的下标；
  其余格忽略；整箱空与 0 格容器 → 没有样品槽）；`ShopInfoTest` +4
  （两条原因的文案不同且都不许合并、正文写「还没设商品」而不是「没有货」、
  空箱时买卖按钮为空而管理入口在、有样品时三个按钮照常）。
- **旧断言一条未改**：`TradePlanTest` / `TradePricingTest` / `UnitsFormTest` / `ShopInfoTest` 的既有用例
  全部原样通过（`emptyBoxReason()` 的字面量与调用点都没动，新增的只是另一条原因）。

### 真机项（**未验**）

下面一条都没有在 BDS 上跑过，只有纯逻辑测试。逐条写清退路，**不许当成已验**：

1. **清空模板箱 → 点牌看到「还没设商品」且没有买卖按钮 → OP 放一件样品 → 立刻能买** —— **未验**。
   退路：`readShop` 每次重新走 `inspectAttachedContainer` + `firstNonEmptyStack`，所以「放样品后立刻能买」
   等价于「重新点牌能读到新的第一个非空槽位」；如果放完还不行，先看是不是牌子缓存了表单快照
   （本模组不缓存，每次点牌都重读），再看 `ContainerError::Empty` 是否还在报（那一格是否真的进了物品）。
2. **0 号空、1 号放钻石 → 钻石就是样品（不是「没有商品」）** —— **未验**。
   退路：`firstNonEmptyStack` 走 `sampleSlotIndex`，单测覆盖了「0 空 1 有」；真机不一致只可能是
   `mergedSlots` 的顺序与直觉不同（双箱时先附着那一半）——按合并顺序理解，不是按箱子物理格号。
3. **第二格塞几组泥土 → 商品与份数都不受影响** —— **未验**。
   退路：管理员店的 `stockUnits` / `roomUnits` 都是无限（票 01 / 02），`itemCount` 不参与；
   若份数变了，说明某一处又去读了 `itemCount`（`ShopService::readShop` 的 `adminShop` 分支）。
4. **交易前后样品槽逐字段不变（NBT / 数量 / 耐久）** —— **未验**（与票 01 / 02 的真机项重叠）。
   退路：买入不调 `takeMatchingItems`、卖出不调 `putItemsIntoShopBox`，两条路径都不写箱子；
   真机若变了，先看是不是补偿路径（`TradeSnapshots.restoreShopBox`）被触发了。

### 代码审查后的修改（Standards / Spec 两轴并行审 `git diff a737762...HEAD`）

**改掉的**

- Spec 轴：票面「交易前后样品槽逐字段不变（单测断言 `ItemStack` 相等）」这一条**退回未勾**，
  并写明为什么没有这条单测——spec 的 Testing Decisions 明确「不在单测里造容器」，
  单测覆盖的是判定规则（`sampleSlotIndex`），逐字段相同只能真机验。
- Spec 轴：票面真机项退回 `- [ ]` 并标「未验」。

**留着的（有理由）**

- `SampleSlot.h` 只放一个模板函数：它的价值就是「第一个非空」这条规则**只有一处**，
  且谓词是参数才让 0 号空 / 1 号有 / 全空这些情形能在纯逻辑里断言。把它挪进 `ContainerFinderBds`
  就等于回到「规则在 mod 层、单测碰不到」的原状。
