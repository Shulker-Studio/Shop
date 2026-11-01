# 01: 管理表单 + 改买入价（第一条端到端链路）

**What to build:** 店主（或 OP）点自己的商店牌 → 表单上出现「管理商店」→ 点进去是管理表单 → 点「改买入价」→ 填一个整数 → 提交后牌面第 3 行变成新价，**所有人在客户端立刻看到新价**。这一票把「表单判据 → 输入 → 校验 → 写牌 → 重发」整条链路打通，做完就能真机验收。

**Blocked by:** None (can start immediately)

**Status:** resolved

- [x] 管理入口判据用 `canManageShop(shopActorOf(记录XUID, 玩家XUID, 是否OP, adminShop))`，**不是** `ShopFormRole`：`shopFormRoleOf` 对管理员店恒返回 `Visitor`（那是给买卖按钮用的）
- [x] `shopFormButtons` 签名改成 `(data, role, canManage)`：`canManage` 为真时**总是**给管理入口，与 `unavailableReason` 无关（否则管理员店一停业就永久锁死）；买卖按钮仍只看 `role` 与 `unavailableReason`
- [x] 单测这两种组合：`(Visitor, canManage=true, unavailableReason 非空)` → 仍有管理按钮；`(Visitor, canManage=false, 同上)` → 空
- [x] 管理表单正文复用 `ShopInfoData`（店主/商品/每份/买入价/卖出价/可成交份数/余额），**不重复算一遍库存**
- [x] 本票只放一个入口「改买入价」；「改卖出价 / 改件数 / 停业」在 02、03 加，**不要**先放假按钮
- [x] `ShopFormAction` 从 `{Buy, Sell, Manage}` 扩成 `{Buy, Sell, Manage, ManageBuyPrice, ManageSellPrice, ManageAmount, ToggleClosed}`（本票只用到前四个，后三个由 02/03 接）；每个动作一个值，**不靠按钮下标猜**——`SimpleFormHandler` 回的是下标，下标随按钮顺序变
- [x] 「改买入价」用 `CustomForm` 一个输入框，初值 = 当前买入价；**语法只收非负整数或留空**（留空 = 这侧不做生意），不收 `B 10` / `free` / `1.5K`——方向由「哪个输入框」表达
- [x] **读表单回传时别假设类型**：`CustomForm` 的 `input` 回来是 `std::string`（不是数字），下拉回来是**选中项文案**而不是下标。
      先用 `get_if<std::string>` 取字符串，再 `std::from_chars` 转整数；拿不到字符串或解析失败一律按「输入不合法」处理，不许静默当 0。
      P0 已经在 `ShopForm.cpp` 栽过一次（下拉当 `uint64` 读，恒为 -1），照那里的 `choiceIndexForOption` 写法办
- [x] 提交时**从牌面重新解析**当前文本（`parseShopSignText`），不要用打开表单时缓存的 spec：表单开着时牌可能已被别人改过
- [x] 校验复用现成的解析与规范化（`src/shop/ShopSign`）：把解析出的 `ShopSpec` 改掉一个字段，再 `formatShopSign(spec, 原店主名, 商品名)` 出文本。
      注意 `formatShopSign` 是**纯函数**、不返回错误——所以要先把 `ShopSpec` 改对（价、件数），再调它；合法性由改之前那次 `parseShopSignText` 的 `ok()` 与改之后的 `blockSellPriceHigherThanBuy` 判。
      失败只提示、牌面一字不改。**不另写一份格式化**
- [x] 写入用 `setMessageForServerScripingOnly(SignTextSide::Front, text, ownerID)`：普通店传店主 XUID；**管理员店传空串**（它的归属靠「第 1 行等于 `adminShopName` + 编辑者是 OP」，不靠 XUID；流水里管理员店的 `owner.xuid` 也是空串）
- [x] 写完把 `BlockActorDataPacket` 发给**整个维度的所有玩家**：在 `BlockResync` 里加一个**不节流**的变体（现有 `resyncBlockActor` 按「玩家+维度+方块」节流 250ms，直接复用会让连改两次价时有人停在旧文本）。只发给同维度的玩家，不跨维度；操作者本人也在这一批里，不需要再单独发一次
- [x] 上蜡的牌**照常能改**（上蜡只锁手写编辑），正文写一句「这块牌上了蜡：手写编辑仍被锁，这里可以直接改」
- [x] 表单回调仍在服务端线程执行，写世界不脱线程
- [x] 单测：合成函数（正常改买价、留空、非法输入、`S > B` 时改买价仍要判）与 `shopFormButtons` 的两种组合
- [ ] 真机验收三条（**都是未证实的假设，验不过按备注退路处理**）：① OP 代改价后牌面第 1 行与店主 XUID 都还是原主；② 改价 → 停服重启 → 新价还在；③ 上蜡的牌能被这条路径写入
- [x] `xmake build Shop` / `xmake build ShopTests` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 三条假设的退路：①没写 XUID → 自己补写 `mLastEditXuid`；②重启回滚 → 写完显式调 `SignBlockActor::onChanged(region)` 标脏；③上蜡写不进去 → 先判 `mIsWaxed` 并给一句明确提示（别硬写）。
- 牌面第 4 行（商品名）由容器决定，管理表单**不碰**它；第 1 行（店主名）也不碰。
- 「打开商店箱」按钮已否决（spec D3）：店主右键箱子本来就放行，不值得为省一次右键去碰裸包。

## Answer

**2026-11-01 实现完毕。**

### 落地的东西

**纯逻辑（`src/shop`，无 `mc/` 头，`check_pure_logic_headers()` 通过）**

- 新增 `ManageForm.{h,cpp}`：
  - `withBuyPrice(current, input, config)` → 新 `ShopSpec` 或中文原因。输入只收非负整数（`std::from_chars` 且必须整串消费，溢出算非法）或留空；留空 = 这一侧不做生意；两侧都空拒绝（D6）；改完后卖出价高于买入价拒绝。`amount` / `sellPrice` / `adminShop` 原样带过去。
  - `shopManageFormContent(data, waxed)`：直接 `shopFormContent(data)` 打底（不重算库存），上蜡时补一句「这块牌上了蜡：手写编辑仍被锁，这里可以直接改」，再加一行写法提示。
  - `shopManageFormButtons(data)`：本票只有「改买入价（现在 $ 10 / 份）」，无买卖价的店写「不出售」。
- `ShopSign.{h,cpp}`：把 `S > B` 判据抽成公开纯函数 `sellPriceHigherThanBuy(buy, sell, config)`，`parseShopSign` 与管理表单共用一份；配套文案 `sellPriceAboveBuyMessage` 也在这里（`messageFor` 委托给它），规则与措辞各只有一份。
- `Text.h`（新）：`trimView` / `trimCopy`。牌面解析、份数表单、管理表单原本各有一份「去两端空白」，其中两份判定字符还不一样——同一段输入会在一个入口能过、在另一个入口不能过。现在三处共用一份。
- `ShopInfo.{h,cpp}`：`ShopFormAction` 扩成 7 个值（`Buy` / `Sell` / `Manage` / `ManageBuyPrice` / `ManageSellPrice` / `ManageAmount` / `ToggleClosed`，后三个 02/03 接）；`shopFormButtons(data, role, canManage)` —— `canManage` 为真时总是给「管理商店」，买卖按钮仍只看 `role` 与 `unavailableReason`；店主（`Manager`）除管理入口外不给买卖按钮。

**BDS 适配（`src/mod`）**

- `ShopForm.{h,cpp}`：`openShopForm` 多一个 `canManage`；新增 `requestManageForm` / `requestBuyPriceForm`，与交易同一套做法——回调里只捕 XUID / 维度 / 牌坐标，`ServerThreadExecutor` 切回服务端线程后重新 `inspectShop` + `canManageShop` 鉴权，再弹表单。
  提交 `submitBuyPrice`：重新取 `SignBlockActor` → 重新 `parseShopSignText`（不信打开表单时的快照）→ `withBuyPrice` → `formatShopSign(spec, 原第 1 行, 原第 4 行)` → `setMessageForServerScripingOnly(Front, text, ownerID)`（普通店店主 XUID / 管理员店空串）→ `resyncBlockActorToDimension` → 一行成功提示。任何一步失败只发消息，牌面一字不改。
  输入框读回走 `get_if<std::string>`：读不到就是 `nullopt` → 提示「读不到输入框的值」，不当 0、也不当留空。
- `BlockResync.{h,cpp}`：新增 `resyncBlockActorToDimension(region, pos)`，不节流；先让原版生成**一份** `BlockActorDataPacket`（`_getUpdatePacket`，为空才 `onChanged` 后重试一次，仍为空就放弃，绝不自己拼 NBT），再 `ll::service::bedrock::getLevel()->forEachPlayer` 只对同维度的玩家发同一份包。
- `SignInteractListener.cpp`：点牌弹表单时传 `canManageShop(interaction->actor)`。

**文档**：`DESIGN.md` §9.2 新增「拦截预演只发操作者 / 服务端写牌发整维度」两种范围，并写明「上蜡只拦手写编辑，不拦管理表单」；§10.1 补「管理入口不看交易角色」「P1 现状（票 01）」两段。`CHANGELOG.md` 增加本票四条。

### 验证

```
xmake build Shop        # ✅ 无 error / 无 warning
xmake build ShopTests   # ✅
xmake test              # ✅ 1/1 passed
ShopTests.exe           # 224 test cases / 3019 assertions，全过
```

基线是 `207 cases / 2944 assertions`，本票净增 **17 个用例 / 75 条断言**（`tests/shop/ManageFormTest.cpp` 15 个 + `ShopInfoTest.cpp` 2 个）。审查后的改动（`Text.h`、文案去重、注释与文档订正）不改变测试数量，复跑仍是 224 / 3019。

### 审查（code-review skill，两轴）

- **Standards 轴**（固定点 HEAD `7ac949f` + 未提交改动）报了 5 条硬性、4 条判断题，均已在提交前修掉：
  `ManageAmount` 的票号写成 03（应为 02）、`ShopSign.h` 新增注释把「分叉」自动写反、
  `ShopService.h` 的 `role` 注释仍说管理入口由角色派生（与 DESIGN 新口径矛盾）、
  `S > B` 文案两份（判据抽了、话没抽）、DESIGN §9.2 上方仍留着「拿不到就自己拼 NBT」的旧说法（与下方 blockquote 自相矛盾）；
  判断题里采纳了「三份 `trim` 合并」「表单标题后缀统一」「测试 helper 改名 `mentions`」，
  未采纳 `shopFormButtons(data, role, canManage)` 的 Data Clumps 一条：两个入参来自同一个 `ShopActor`，
  但单测需要能表达「管理员店的 OP：交易上是访客、同时能管理」这种组合，拆开写反而更清楚。
- **Spec 轴**（并行 subagent，对照 `spec.md` + 本票逐条核对）：9 条要求全部「做到」，无 02/03 越界。
  唯一一条偏差（`submitBuyPrice` 里 `editorIsOp` 恒 `true`）已落在注释里说清：它只影响解析出的 `adminShop`，
  而 `formatShopSign` 不看这个字段，写牌用的 XUID 来自 `inspectShop` 读出的真值。

### 三条未证实的假设：都**没验**（本次只做了构建与单测）

| 假设 | 状态 | 验不过的退路 |
| --- | --- | --- |
| ① `setMessageForServerScripingOnly` 会写 `mLastEditXuid` | 未验（头文件推断） | 自己补写 `mLastEditXuid` |
| ② 改价能落盘（重启后还在） | 未验 | 写完显式调 `SignBlockActor::onChanged(region)` |
| ③ 上蜡的牌能被这条路径写入 | 未验 | 先判 `mIsWaxed` 并给一句明确提示（别硬写） |

按票面要求，**没有**为了让测试过而改生产逻辑：`onChanged` 只在「`_getUpdatePacket` 拿不到」的退路里调，写牌之后没有额外标脏。

### 没做的事

- `ManageSellPrice` / `ManageAmount` / `ToggleClosed` 只在枚举里占位，没有任何表单发出它们（02、03 接）。
- 价格行末尾的停业段 `:x`（解析、规范化、交易拒绝）整块留给 03——本票若先把 `:x` 认成合法价格行，会出现「牌面写着停业但照常能交易」的半成品状态。
- 打开商店箱按钮按 D3 否决，未做。
