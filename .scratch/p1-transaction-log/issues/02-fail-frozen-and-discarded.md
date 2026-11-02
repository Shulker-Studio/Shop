# 02: 失败与冻结也留痕 + 被销毁物品留 NBT

**What to build:** 被拒的交易、以及补偿失败被冻结的交易，都各留一行（`fail` / `frozen`）；管理员店卖出的货被销毁时，往 `discarded-YYYY-MM-DD.jsonl` 写一行含完整 SNBT 的记录。

**Blocked by:** 01（`discarded` 的**真机验收**还要等 `p1-admin-unlimited-stock` 票 02 落地——在那之前普通店没有"销毁"这件事，只能用单测覆盖）

**Status:** resolved

- [x] `Rejected → fail` + `reason`（由 `TradeRejection` 映射成机器可读短名，如 `no_money` / `no_stock` / `no_space` / `owner_offline` / `shop_closed`，**不把中文文案写进 reason**）
- [x] `Broken → frozen` + `reason: compensation_failed`，且必须与那条 `fatal` 日志同时出现
- [x] `NeedsConfirm` **不写**（不是终态，玩家可能直接关掉确认表单）
- [x] 同一对「玩家 + 商店」被冻结后的后续提交记 `fail` + `reason: frozen_pair`（不重复写 `frozen`）
- [x] 免费店（`gross = 0` / `total = 0`）仍记一行 `ok`（"0 元的成交"是有意义的历史）
- [x] 管理员店：`admin: true`、`owner.name = adminShopName`、`owner.xuid = ""`、`tax = 0`、`backend` 如实写
- [x] **`discarded` 文件**：管理员店卖出方向（货被销毁）额外写 N 行，一行一件物品堆，共用同一个 `ts` 与 `shop`；字段 `ts / shop / player / item{id,aux,name,nbt}`，`nbt` 是 `stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`；普通店与管理员店买入方向都不写
- [x] 两个文件同一套「写失败只 warn 一次、交易照常成功」的策略（压缩、以及 `transactionLog.enabled=false` 时两个一起停见票 03：本票把两个文件接到**同一个**落盘入口，票 03 的开关加在那一个入口上就同时管住两个）
- [x] 单测：`fail` / `frozen` / `frozen_pair` 三种行、`admin` 组合校验、`discarded` 行的序列化（长 SNBT 原样透传、仍是一行）
- [ ] 真机（**未验**）：余额不足一次 → `fail` + `no_money`；管理员店卖一次 → 业务流水一条 `ok` + `discarded` 里对应那件货的 NBT
- [x] `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 顺序验证：日志里绝不出现「`ok` 之后又跟进一条退款」——`ok` 是终态。
- `discarded` 与业务流水同一套压缩规则（03），不要各写一份。

## Answer

### 落地了什么

**纯逻辑（`src/shop`，无 `mc/` 头）**

- `TradeRecord.h/.cpp` 新增 `tradeRejectionReasonName(TradeRejection)`：拒绝原因 → 机器可读短名
  （`shop_closed` / `owner_offline` / `no_stock` / `no_money` / `no_space` / `over_limit` /
  `invalid_request`），`None` 返回空串（它不是拒绝）。spec 的「不把中文文案写进 `reason`」就靠它：
  文案（`tradeRejectionMessage`）随时会改，历史流水的键不能跟着改。
- 新增 `JsonLine.h/.cpp`（`jsonLine(nlohmann::ordered_json const&)`）：**两个流水文件共用同一份**
  「一行一个对象、`\n` 结尾、中文不转义、非法 UTF-8 换 U+FFFD」的实现。
  `DiscardRecord.cpp` 那 60 行手写转义（`appendJsonString` / `appendKey`）随之删掉，
  `discardRecordJson` 改成装 `ordered_json` 再走 `jsonLine`——这是**收敛**，不是新写第二套：
  先前 `DiscardRecord` 手拼、`TradeRecord` 用库，正是「两套 JSON 拼装」的开头。
  **`DiscardRecordTest` 的 5 条既有断言一条未改、逐字节仍绿**（键序、转义、中文原样、nbt 透传、文件名）。

**BDS 适配（`src/mod`）**

- `TradeResult` 不再带「造好的记录」，改成带两样东西：`std::optional<TradeFacts> logFacts`
  （谁、在哪家店、买了什么、多少钱一份）与 `std::string_view logReason`（短名）。
  **结论与 `ts` 由 `finishTrade` 补**——它是唯一知道终态的地方，三态映射因此只有一处：
  `Done → ok` / `Rejected → fail` / `Broken → frozen`，`NeedsConfirm → 不写`（`tradeLogResultOf`）。
- 事实分两段填：`basicTradeFacts`（方向、牌子、交易者——牌没了也拿得到）与 `fillTradeFacts`
  （商店快照到手后补店主、商品、单价、后端）。每一条失败路径都用 `withFacts(...)` 带上事实，
  所以 `fail` 行也指向具体的人和牌，不是一条无主记录。
- 拒绝原因两份表，分工写在代码里：**由数字判定**的走纯逻辑的 `tradeRejectionReasonName`
  （`planTrade` 的判据）；**世界状态**的走 mod 侧新的 `RejectReason`（`shop_missing` /
  `shop_unavailable` / `owner_unknown` / `self_trade` / `frozen_pair` / `creative_mode` /
  `no_backend` / `no_system_account` / `box_missing` / `no_product` / `item_mismatch` /
  `player_funds_unknown` / `owner_funds_unknown` / `payment_failed` / `tax_failed` /
  `compensation_failed`）。`reject()` 三个重载（`RejectReason` / `TradeRejection` / 已是短名）
  让每个调用点只写枚举，不写字符串字面量。
- `broken()` 自己把 `logReason` 钉成 `compensation_failed`：`frozen` 行与那条 `fatal` 日志
  **必然成对**，服主拿 `reason` 就能对上事故说明。
- 冻结名单命中的后续提交 → `fail` + `frozen_pair`（不是又一条 `frozen`）：事故只发生一次，
  记两次会让人去查两次。
- `undoAndRefund` 多一个 `reason` 参数（`no_stock` / `no_space` / `tax_failed`），
  退款成不成功都不改变「为什么没卖成」。
- `DiscardLog::writeDiscardRecords` 删掉自己那份建目录/开文件/`flush`/warn，改成把整批行交给
  `appendLogLines`——与业务流水**同一套**落盘策略（一次一个 `ofstream`、`flush`、关；
  失败只 warn 且按失败种类同进程一次）。**没有**新建第二个 writer、也没有第二处循环写文件。
- 免费店（`total == 0`）照旧写一行 `ok`：0 元的成交是有意义的历史（管理员店期限价、白送）。
- 管理员店：`admin: true`、`owner.name = adminShopName`、`owner.xuid = ""`、`tax = 0`
  ——后两项由 `composeTradeRecord` 强制，前两项由 `fillTradeFacts` 按 `config.adminShopName` 取。

**顺手做的**：`src/mod/TradeService.cpp` 过了一遍 `clang-format`（原本在 HEAD 上就有 31 行不合格式）。
**只有对齐与 include 顺序，没有语义改动**；构建与测试前后都全绿。

### 测试

- `xmake build Shop`：ok（无 warning）
- `xmake build ShopTests`：ok
- `xmake test`：`ShopTests/default passed`
- `./build/windows/x64/release/ShopTests.exe`：**307 cases / 3436 assertions，全绿**
  （上一票 304 / 3420 → **+3 cases / +16 assertions**；本次分支基线 296 / 3382）
- 新增 3 个用例：拒绝原因短名表（7 个值 + `None` 为空）、`frozen_pair` 是 `fail` 而不是 `frozen`、
  免费店仍是 `ok` 且 `gross`/`tax`/`total` 都是 0。`fail` / `frozen` 两种行与 `admin` 组合校验
  由票 01 那三个用例覆盖（同一份 `composeTradeRecord` + `tradeRecordProblem`）。
- **既有断言一条未改**：`DiscardRecordTest`（5 条，含长 SNBT 原样透传、换行不撕行）在
  `DiscardRecord.cpp` 换成 `nlohmann::ordered_json` 之后逐字节仍绿——这就是「收敛没改语义」的证据。

### 真机项（**未验**）

两条都没有在 BDS 上跑过。**不许当成已验**：

1. **余额不足一次 → `fail` + `no_money`** —— **未验**。
   退路：先确认钱那一步确实被拒（玩家会收到「余额不足」的提示）。若流水里没有这行，
   看 `/shop debug on` 之后 `trade_result` 的 `outcome` 是不是 `fail`；`outcome` 是 `fail`
   却没有流水行，说明 `result.logFacts` 没被填（`withFacts` 漏在某条返回路径上），
   按该路径在 `buyUnits` / `sellUnits` 里补一次即可。
2. **管理员店卖一次 → 业务流水一条 `ok` + `discarded` 里对应那件货的 NBT** —— **未验**。
   退路：两个文件都在 `plugins/Shop/logs/`；`ok` 行的 `admin` 应为 `true`、`owner.xuid` 为空串、
   `owner.name` 为 `adminShopName`、`tax` 为 0。`discarded` 缺行而业务流水有行，说明
   `TradeResult::discarded` 没被填（`sellUnits` 的 `info.adminShop` 分支）；两行都缺，
   看 warn（目录建不出来 / 文件打不开，同进程各只提示一次）。
3. **补偿失败 → `frozen` + `compensation_failed`，且与 `fatal` 日志同时出现** —— **未验**（难造）。
   退路：`broken()` 是唯一出口，`fatal`、通知 OP、冻结名单、`logReason` 四件事在同一函数里，
   不可能只出现其中一件；真要复现，用一个总是返回失败的后端（`economy.backend = null` 是
   「读不到余额」，不会走到补偿失败），实际只能靠磁盘/后端故障时的事故日志核对。

### 这一票**没有**做的（票 03 的地盘）

`transactionLog.enabled = false` 时两个文件一起停、以及 `compressAfterDays` 的 gzip：
这两项都要先有 `transactionLog` 配置块，而配置块按票面由票 03 定义。本票只把两个文件
**接到同一套落盘策略**上（同一份 `appendLogLines`、同一种「失败只 warn 一次」），
所以票 03 的开关只要加在这一个入口上就同时管住两个文件——不会出现「只关了一个」。

### 真机反馈后的修复（`item.nbt` 写成了 `"4 entries"`）

**现象**：`discarded-*.jsonl` 里每行的 `item.nbt` 都是 `"4 entries"` 这种字符串，附魔/改名/耐久全不在。

**根因**：`Tag::toString()` 在 BDS 里返回的是给调试打印用的**摘要**（一个钻石正好 4 条 NBT：
`Count` / `Damage` / `Name` / `wasPickedUp`），不是 SNBT。真正的 SNBT 出口是
`Tag::toSnbt(SnbtFormat)`；默认的 `SnbtFormat::PrettyFilePrint` 会带换行与缩进，
塞进 NDJSON 虽然仍是合法 JSON（换行被转义），但一行几十 KB 已经够难读，所以用
`SnbtFormat::Minimize`（紧凑单行）。

**顺带改的**：`SaveContext` 从 `Clone` 改成 `SaveToDisk`。留档要的是「这件东西在存档里长什么样」，
而 `Clone` 是「为了复制一件物品」的语义——`SaveContext` 只有 `mSaveUseCase` 一个字段，
差别全在这一个枚举上，没有别的地方能补救。

（这一条只能在真机上验：`CompoundTag` 的实现要 BDS 才在。修完后 `nbt` 形如
`{Count:1b,Damage:0s,Name:"minecraft:diamond",wasPickedUp:0b}`，已列进验收清单第 3 条的判据。）

**缺口已闭合**（票 03 落地后补记）：`transactionLog.enabled = false` 时两个文件一起停、以及
`compressAfterDays` 的 `.jsonl.gz` 压缩，两项都在票 03 落在 `LogFile::appendLogLines` 这一个入口上。
本票的 `discarded` 不需要任何额外接线就同时受开关与轮转管住。
