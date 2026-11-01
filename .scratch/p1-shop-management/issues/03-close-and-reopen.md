# 03: 管理表单里的停业 / 恢复营业

**What to build:** 管理表单多一个按钮：营业时写「停业」，停业时写「恢复营业」。停业期间谁都不能买卖，牌面、归属、库存、保护全都不动；恢复后原价一分不差。停业状态存在牌面上，重启后仍然有效。

**Blocked by:** 02

**Status:** resolved

- [x] 牌面表示：价格行末尾追加 `:x`（`B 10 : S 5 :x`、单侧 `S 5 :x`）。大小写不敏感、`: x` 也认，规范化恒输出 `:x`
- [x] 解析规则是「**先剥离最后一段**」：最后一段 trim 后等于 `x` 则判停业并剔除，**剩下的再按原文法解**且要求至少剩一个价。现有 `segments.size() > 2` 的检查必须挪到剔除之后（否则 `B 10 : S 5 :x` 会先被拒）；只有 `x` 没有价（`:x`、`x`）按非法处理
- [x] 停业牌仍然 `looksLikeShopSign()`（第 3 行含 `B`/`S`）→ 非店主的改文本 / 染色 / 上蜡 / 开箱 / 拆箱 / 拆牌全部照旧被拦
- [x] 新增 `TradeRejection::ShopClosed` + 文案「这家店暂停营业」，判据放在 `planTrade` **最前面**（比任何上限都优先）；点牌正文写同一句、一个买卖按钮都不给；提交阶段也要拒（防旧表单）
- [x] `ShopInfoData` 加 `bool closed`，`shopInfoLines` 在停业时多一行「这家店暂停营业」
- [x] 停业时管理入口**照给**（01 已实现，这里补一条回归断言）：管理员店的 OP 是 `Visitor`，若入口也消失就永久停业，只能拆牌
- [x] 手写编辑写 `:x` 也生效（就是合法价格行写法，不需要额外权限），且不会被规范化抹掉
- [x] 单测：`:x` 的解析与规范化（含 `B 10 : S 5 :x`、`S 5 :x`、`B 10:x`、只有 `x`）、停业时 `planTrade` 返回 `ShopClosed`、停业时按钮集合为空但管理入口仍在、恢复后 spec 与停业前相等
- [ ] 真机：停业 → 买家点牌看到「暂停营业」且无买卖按钮 → 重启 → 仍然停业 → 恢复营业 → 原价不变
- [x] 文档：`DESIGN §5.2` 价格行文法补 `x` 段、`§13` 加一条验收场景；`p1-transaction-log` 的 `reason` 加 `shop_closed`
- [x] `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 为什么不用「清空价格行」：第 3 行没有 `B`/`S` 就不再像商店牌，保护整体失效，别人能改牌、能拆箱——比停业严重得多。
- 牌面上只有 `:x` 这个机器标记：第 3 行宽度放不下中文，玩家看得懂的「暂停营业」在表单正文与拒绝文案里。
- 停业 ≠ 关店：拆牌才是关店（归属与绑定一起没）。

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- `ShopSign.*`：`PricePair` / `ShopSpec` 各加 `bool closed`。`parsePriceLine` 改成「**先剥离最后一段**」——新抽的
  `splitPriceSegments` 先看末尾一段 trim 后是不是 `x`（大小写不敏感），是就剔除并记 `closed`，**然后**才检查
  「段数 1..2」，所以 `S 5 :x`（第二段就是停业段）与 `B 10 : S 5 :x` 都能过；只有标记没有价（`x`、`:x`）仍然是非法。
  `formatShopSign` 在价格行末尾恒写 ` :x`（两侧都空的 spec 本来就不合法，不为它编一个 `x`），解析与格式化因此互逆。
- `TradePlan.*`：新增 `TradeRejection::ShopClosed` + `tradeRejectionMessage` 的「这家店暂停营业」（两个方向同一句）；
  `TradeRequest` 加 `bool closed`，`planTrade` 的第一条判据就是它（比缺货、没钱、份数上限、甚至「输入不合法」都优先）。
- `ShopInfo.*`：`ShopInfoData` 加 `bool closed`（与 `unavailableReason` 故意分开：一个是店主拉的闸，一个是后端/在线状态）；
  `shopInfoLines` 在 `closed` 时紧跟店主行多一行「这家店暂停营业」；`shopFormButtons` 在 `closed` 时一个买卖按钮都不给，
  但管理入口仍然先按 `canManage` 无条件给出（D5 的回归）。
- `ManageForm.*`：把四个动作收进 spec「Testing Decisions」的那条缝 `applyManageEdit(current, ManageEditRequest, config)`
  → `ManageEdit`（新定义 + 原因），`ManageEditRequest` 四个 `optional` 字段各表示「这次改这一格」。`withBuyPrice` /
  `withSellPrice` / `withAmount` / `withToggledClosed` 全是它的薄包装：校验（非负整数或留空、两侧都清空、`S > B`）
  只有一份，停业不豁免其中任何一条。`shopManageFormButtons` 多第四个按钮，按 `data.closed` 输出「停业」或「恢复营业」。

**BDS 适配（`src/mod`）**

- `ShopService::readShop` 把 `parsed.spec->closed` 填进 `ShopInfoData.closed`（牌面是唯一数据源，不存内存）。
- `TradeService::prepareCommon`：读牌后立刻按 `ShopClosed` 拒（消息优先于「店主不在线」「商店箱里没有货」，同时充当提交阶段的闸口），
  并把 `closed` 装进 `TradeRequest`。两处判据**故意都保留**：`planTrade` 是纯逻辑层的权威判据与单测缝，不靠
  「调用方已经拦过」这个前提；`prepareCommon` 是提交路径的用户可见闸口。`TradeService.cpp` 的注释写明了
  这一点，免得下次审查又把它当死逻辑删掉。
- `ShopForm`：`onButton` 的 `ToggleClosed` 接上新的 `requestManageToggleClosed`。停业没有输入框，但它**不进第二套写牌链**：
  直接走 `submitManageEdit`（重新取牌 → 重新 `parseShopSignText` → 重新 `inspectManageTarget` → `applyManageEdit` →
  `formatShopSign` → `setMessageForServerScripingOnly`（第 1、4 行沿用原文）→ `resyncBlockActorToDimension`），
  成功提示由 `closedEditMessage` 给。`manageInputField` 的 `ToggleClosed` case 保持「没有输入框」，路径不变。
- **已知行为**（不是缺陷，但没做额外刷新）：管理表单是**打开那一刻**的快照，按钮文案也是；连着点两次「停业」
  会先停业、再恢复（每次服务端都重新读牌面，并回一句当前结果）。要看到最新按钮得重新点「管理商店」。
  与改价那条链一致（改完不重开表单），二次确认的代价远大于收益。

**文档**：`DESIGN §5.1` 第 3 行补 `:x`、`§5.2` 文法补 `closed` 段与「先剥离最后一段」的顺序说明 + 停业≠关店、
`§10.1` 补「P1 现状（票 03 落地后）」、`§13` 新增「停业 / 恢复营业」场景 29e–29i；`GLOSSARY.md` 新增
「停业」「恢复营业」两条（含 `_Avoid_`: 关店=拆牌）；`.scratch/p1-transaction-log/spec.md` 的 `reason` 明确
`TradeRejection::ShopClosed` → `shop_closed`；`CHANGELOG.md` 记一条。

### 代码审查后的修改（Standards / Spec 两轴并行审 `git diff HEAD`）

改掉的：`ManageForm.h` 注释里指向已改名的 `withPrice`（→ `applyPrice`）；DESIGN §5.2 与 `ShopSign.h` 的文法把价格段写成
可省（与「至少剩一个价」矛盾，改成 `segment (":" segment)? [":" closed]`）；`applyPrice` 的 `optional<ManageEdit>`
反语义（`nullopt` = 成功）改成 `optional<std::string>` 的错误文本；`ShopInfo.cpp` 里两个逐字相同的 return 合成的
`data.closed || !data.unavailableReason.empty()`；停业标记提成 `kClosedMarker`（`PriceSegments::segments` → `parts`）；
`prepareCommon` 里第二次 `ShopClosed` 判断：**先删后建**，最后保留两处并写明理由（`TradeService.cpp` 注释）：
`prepareCommon` 是提交路径的用户可见闸口，`planTrade` 是纯逻辑层的权威判据与单测缝（票要求的就是这两道）。
`GLOSSARY.md` 补「停业 / 恢复营业」（含 _Avoid_：关店=拆牌）。

留着的（有理由）：`withToggledClosed` 的不使用 `input` 参数（对齐 `ManageChange::apply` 的函数指针类型，改类型会牵扯
三张输入表单）；`kInputHint` 补「（停业请用下面的按钮）」（与票 02 那条提示同类，指给人看新按钮）；
管理表单四个按钮的总数断言散在 4 个用例（只影响以后加按钮时的测试改动量）。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**260 cases / 3254 assertions，全绿**
  （基线 240 / 3131 → **+20 cases / +123 assertions**）
- 分布：`ShopSignTest` 新增 8 个用例（`B 10 : S 5 :x` / `S 5 :x` / `B 10:x` / `10 :x` / `: X` / `:  x  `、
  只有 `x` 的四种非法、带 `:x` 仍过原文法、规范化恒写 `:x`、解析↔格式化互逆、手写 `:x` 生效且不被抹掉、
  停业牌仍 `looksLikeShopSign`）；`TradePlanTest` 新增 4 个（`ShopClosed` 压过上限与输入校验、卖出方向、恢复后照旧、
  文案精确）；`ShopInfoTest` 新增 3 个（正文多一行、买卖按钮为空、管理入口回归）；`ManageFormTest` 新增 5 个
  （按钮二选一、只翻 `closed` 位、`:x` 落牌面与再解析、`applyManageEdit` 一次只改一格、改价+停业同提交时校验仍生效）。
  三个「管理表单按钮」旧用例的 `buttons.size()` 由 3 改为 4（多出来的第四个就是本票的按钮），逐项断言未动。

### 真机项（**未验**）

下面一条都没有在 BDS 上跑过，只有纯逻辑测试。逐条写清退路，**不许当成已验**：

1. **停业 → 买家点牌看到「这家店暂停营业」且无买卖按钮 → 重启 → 仍然停业 → 恢复营业 → 原价不变** —— **未验**。
   退路：`closed` 完全由牌面第 3 行的 `:x` 决定（`readShop` 每次重新 `parseShopSignFront`），所以「重启后仍然停业」
   与「恢复后原价不变」等价于「写牌落盘」这一条：
   - 若重启后牌面回滚到停业前（`setMessageForServerScripingOnly` 没标脏），退路是写完后显式调
     `signActor->onChanged(region)`（DESIGN §10.1 票 01 已记下这个待验项）。
   - 若停业按钮点了没反应，先看 `inspectManageTarget` 的鉴权分支（它与管理改价共用，改价能成就说明问题在别处）。
2. **停业后归属不变**（`setMessageForServerScripingOnly` 是否真写 `mLastEditXuid`）—— **未验**。
   退路：与票 01 同一条——自己补写 `mLastEditXuid`；本票不新增写入路径，风险与票 01 相同。
3. **上蜡的牌能否被停业写入** —— **未验**。退路：管理表单是并列的第二条写牌路径，上蜡只锁手写编辑（DESIGN §9.2 / spec D4）；
   若实测写不进去，退路只能是拆牌重立，且不得宣称已支持。
4. **非店主在停业牌上的防护照旧**（改文本 / 染色 / 上蜡 / 开箱 / 拆箱 / 拆牌）—— 纯逻辑侧只断言了
   `looksLikeShopSign()` 仍为真（保护判据只看「像不像商店牌」），引擎侧拦截**未验**，退路同 P0 票的验收路径。
