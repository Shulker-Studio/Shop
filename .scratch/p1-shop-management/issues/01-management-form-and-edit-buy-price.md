# 01: 管理表单 + 改买入价（第一条端到端链路）

**What to build:** 店主（或 OP）点自己的商店牌 → 表单上出现「管理商店」→ 点进去是管理表单 → 点「改买入价」→ 填一个整数 → 提交后牌面第 3 行变成新价，**所有人在客户端立刻看到新价**。这一票把「表单判据 → 输入 → 校验 → 写牌 → 重发」整条链路打通，做完就能真机验收。

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [ ] 管理入口判据用 `canManageShop(shopActorOf(记录XUID, 玩家XUID, 是否OP, adminShop))`，**不是** `ShopFormRole`：`shopFormRoleOf` 对管理员店恒返回 `Visitor`（那是给买卖按钮用的）
- [ ] `shopFormButtons` 签名改成 `(data, role, canManage)`：`canManage` 为真时**总是**给管理入口，与 `unavailableReason` 无关（否则管理员店一停业就永久锁死）；买卖按钮仍只看 `role` 与 `unavailableReason`
- [ ] 单测这两种组合：`(Visitor, canManage=true, unavailableReason 非空)` → 仍有管理按钮；`(Visitor, canManage=false, 同上)` → 空
- [ ] 管理表单正文复用 `ShopInfoData`（店主/商品/每份/买入价/卖出价/可成交份数/余额），**不重复算一遍库存**
- [ ] 本票只放一个入口「改买入价」；「改卖出价 / 改件数 / 停业」在 02、03 加，**不要**先放假按钮
- [ ] `ShopFormAction` 从 `{Buy, Sell, Manage}` 扩成 `{Buy, Sell, Manage, ManageBuyPrice, ManageSellPrice, ManageAmount, ToggleClosed}`（本票只用到前四个，后三个由 02/03 接）；每个动作一个值，**不靠按钮下标猜**——`SimpleFormHandler` 回的是下标，下标随按钮顺序变
- [ ] 「改买入价」用 `CustomForm` 一个输入框，初值 = 当前买入价；**语法只收非负整数或留空**（留空 = 这侧不做生意），不收 `B 10` / `free` / `1.5K`——方向由「哪个输入框」表达
- [ ] **读表单回传时别假设类型**：`CustomForm` 的 `input` 回来是 `std::string`（不是数字），下拉回来是**选中项文案**而不是下标。
      先用 `get_if<std::string>` 取字符串，再 `std::from_chars` 转整数；拿不到字符串或解析失败一律按「输入不合法」处理，不许静默当 0。
      P0 已经在 `ShopForm.cpp` 栽过一次（下拉当 `uint64` 读，恒为 -1），照那里的 `choiceIndexForOption` 写法办
- [ ] 提交时**从牌面重新解析**当前文本（`parseShopSignText`），不要用打开表单时缓存的 spec：表单开着时牌可能已被别人改过
- [ ] 校验复用现成的解析与规范化（`src/shop/ShopSign`）：把解析出的 `ShopSpec` 改掉一个字段，再 `formatShopSign(spec, 原店主名, 商品名)` 出文本。
      注意 `formatShopSign` 是**纯函数**、不返回错误——所以要先把 `ShopSpec` 改对（价、件数），再调它；合法性由改之前那次 `parseShopSignText` 的 `ok()` 与改之后的 `blockSellPriceHigherThanBuy` 判。
      失败只提示、牌面一字不改。**不另写一份格式化**
- [ ] 写入用 `setMessageForServerScripingOnly(SignTextSide::Front, text, ownerID)`：普通店传店主 XUID；**管理员店传空串**（它的归属靠「第 1 行等于 `adminShopName` + 编辑者是 OP」，不靠 XUID；流水里管理员店的 `owner.xuid` 也是空串）
- [ ] 写完把 `BlockActorDataPacket` 发给**整个维度的所有玩家**：在 `BlockResync` 里加一个**不节流**的变体（现有 `resyncBlockActor` 按「玩家+维度+方块」节流 250ms，直接复用会让连改两次价时有人停在旧文本）。只发给同维度的玩家，不跨维度；操作者本人也在这一批里，不需要再单独发一次
- [ ] 上蜡的牌**照常能改**（上蜡只锁手写编辑），正文写一句「这块牌上了蜡：手写编辑仍被锁，这里可以直接改」
- [ ] 表单回调仍在服务端线程执行，写世界不脱线程
- [ ] 单测：合成函数（正常改买价、留空、非法输入、`S > B` 时改买价仍要判）与 `shopFormButtons` 的两种组合
- [ ] 真机验收三条（**都是未证实的假设，验不过按备注退路处理**）：① OP 代改价后牌面第 1 行与店主 XUID 都还是原主；② 改价 → 停服重启 → 新价还在；③ 上蜡的牌能被这条路径写入
- [ ] `xmake build Shop` / `xmake build ShopTests` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 三条假设的退路：①没写 XUID → 自己补写 `mLastEditXuid`；②重启回滚 → 写完显式调 `SignBlockActor::onChanged(region)` 标脏；③上蜡写不进去 → 先判 `mIsWaxed` 并给一句明确提示（别硬写）。
- 牌面第 4 行（商品名）由容器决定，管理表单**不碰**它；第 1 行（店主名）也不碰。
- 「打开商店箱」按钮已否决（spec D3）：店主右键箱子本来就放行，不值得为省一次右键去碰裸包。

## Answer

（待实现）
