# 03: 点牌看见商店

**What to build:** 点一块有效的商店牌，弹出表单显示店主名、商品名、每份件数、买价与卖价、当前可成交份数与玩家余额。余额在后端未就绪时显示占位。店主点自己的牌看到的是管理入口，没有买卖按钮。

同时验证一件必须实测的事：在交互阶段取消方块交互，是否真的不会弹出原版的告示牌编辑界面。

**Blocked by:** 02

**Status:** resolved

- [x] 点有效商店牌 → 弹信息表单（"不弹原版编辑界面"待真机确认，见 Answer 末节）
- [x] 表单里的店主、商品、份量、价格、库存与实际一致
- [x] 店主点自己的牌 → 管理入口，无买卖按钮
- [x] 点普通告示牌 → 照常弹原版编辑界面，不受影响
- [x] 连续快速点牌 → 冷却期内只处理一次
- [ ] 记录实测结论：交互阶段取消是否阻止编辑界面弹出 → 无法在本地验证，已连同 Plan B 写进 12 的待验清单

## Answer

**已实现（`xmake` 构建通过，`xmake test` 67 cases / 2280 assertions 全绿）**

- `src/shop/TradeGate.{h,cpp}`：纯逻辑连点冷却。键是「玩家 XUID + 牌子坐标」，时钟由调用方注入（毫秒），窗口是「距上次**放行**满 `cooldownMs` 即再放行」，被拒的点击不刷新窗口。冷却为 0 时完全旁路。表超过 `kMaxTracked`（512）先清过期项、再清空，作为防刷兜底。
- `src/shop/ShopInfo.{h,cpp}`：纯逻辑表单文案。`shopFormTitle` / `shopInfoLines` / `shopFormContent` / `shopFormButtons`，全部只吃 `ShopInfoData`，不含引擎类型。
- `src/shop/TradePricing.*`：新增 `stockUnits(itemCount, amountPerUnit)`——可成交份数 = 件数 / 每份件数（向下取整），每份件数非法时返回 0。
- `src/mod/ContainerFinderBds.*`：`ContainerLookup` 增加 `itemCount`（合并库存里与商品**严格匹配**的总件数）。商品仍是「第一个非空槽位的物品」（附着那一半优先），统计用 `getId() + getAuxValue() + CompoundTag::equals` 严格比较（DESIGN §5.3），混装箱子里只有商品本身参与计价。`ContainerRef::size` 从只写字段变成合并遍历的槽位边界（`forEachMergedSlot`），「合并库存多少格」只有一处定义。
- `src/mod/ShopOwner.{h,cpp}`：店主判定。读正面 `SignBlockActor::Text::mLastEditXuid` → `ll::service::PlayerInfo::fromXuid` 取当前名字，查不到退回牌面第 1 行。管理员店按**牌面文字**（第 1 行 == `adminShopName`）判定，不看此刻是谁在看。另含 `ensureOwnerRecorded`：编辑落地后补写一次 ownerID（见下）。
- `src/mod/ShopService.*`：新增 `inspectShop(region, pos, viewer, config)`，返回 `ShopSnapshot{ShopInfoData, ShopFormRole}`；`std::nullopt` 表示「现在不该接管这次交互」。只读，不写方块、不发消息。
- `src/mod/ShopForm.{h,cpp}`：`ll::form::SimpleForm` 渲染。标题与正文来自纯逻辑；按钮回调只发消息（本期「交易功能待实现（04/05）」「商店管理功能待实现」），不碰方块和容器，所以回调跑在哪个线程都安全。
- `src/mod/SignInteractListener.{h,cpp}`：`ll::event::player::PlayerInteractBlockEvent`。判定顺序是「是告示牌 → 玩家看的是正面 → 正面是合法商店牌 → 附着格是可用容器」，四步有一条不成立就**直接返回，一个字都不改**；全成立才 `cancel()` + 冷却判定 + 弹表单。
- `src/mod/SignEditListener.cpp`：新增 `PlayerEditSignAfterEvent` 监听，落地 `ensureOwnerRecorded`（DESIGN §5.4 的 fallback）。
- `src/mod/Config.*`：新增 `shopInteractionCooldownMs`（默认 250，负数视为配置非法），冷却毫秒数只在这里定义一次。
- `Shop.cpp`：enable/disable 注册与移除交互监听；编辑监听现在管 Before + After 两个 `ListenerPtr`。

**决策与偏离**

- **店主 XUID 的补写**：包路径（`PlayerEditSignBeforeEvent` + 改 `text()`）不写 `mLastEditXuid`——`SignBlockActor::setMessageForServerScripingOnly(side, text, ownerID)` 要显式传 ownerID 就是证据。所以按 DESIGN §6.5 第 3 条在 After 事件里补一次：只写 ownerID、文本不变、不 `refresh`。三个条件同时成立才写：正面是合法商店牌、附着容器可用、`mTextFront` 存在且**记录的 XUID 与本次编辑者不一致**——普通牌、非法牌、失去容器的牌一个字都不碰。本函数不判「编辑者是不是店主」，那是 06 的事（见已知边界）。
- **管理员店判定改用牌面文字**：不能拿「点击者的 OP 状态」冒充 `editorIsOp` 传给解析器，否则非 OP 访客会把已有的管理员店解析成普通店（店主名会显示成创建者的名字）。现在的判定是「已确认合法商店 + 第 1 行 == `adminShopName`」，与规范化路径互相印证（规范化时普通店的第 1 行会被写成编辑者名字，所以牌面上留着 `Admin Shop` 的只可能是 OP 建的管理员店，或外部工具写入）。外部工具写入这种边角情况 P0 不仲裁。
- **信息直接放正文，不设「商店信息」按钮**：ticket 要求「点牌就看见」店主/商品/份量/价格/库存/余额，正文直接列出来最省一次点击；DESIGN §10.1 原来列的 `商店信息` 按钮取消，已同步改文档。按钮只保留动作：买家 `购买`/`出售`（缺哪一边价格就没有哪个按钮），店主 `管理商店`。
- **标题用中文 `<店主> 的商店`**：ticket 正文里记的是 `<owner>'s shop`，DESIGN §10.1 写的是 `<店主> 的商店`，按 DESIGN 来；管理员店标题是 `管理员商店`。
- **OP 看到的是管理入口**：按 DESIGN §9.2 的表格（OP 列「同店主」）与 §11（OP 有全部权限），`isOperator()` 直接给 Manager 角色。副作用是 **OP 现在无法从任何商店买/卖**——如果 04/05 需要 OP 也能交易，那一步再把「管理」和「不能与自己交易」拆开（比如管理员店把 OP 当买家）。已记在这里备查。
- **余额一律占位**：经济后端在 10 才落地，`info.balance` 恒为 `std::nullopt`，正文写 `余额：暂不可用（经济后端待接入）`，不编造数字。货币符号暂时用 `ShopInfoData` 的默认 `$`，等 10 把 `economy.currencySymbol` 接进配置。
- **`ContainerRef::size` 有了真读者**：合并遍历用它当槽位边界（`forEachMergedSlot`），双箱 54 格的「合并库存多少格」不再有两处说法。顺带确认 `BarrelBlockActor` 派生自 `ChestBlockActor`（`mc/world/level/block/actor/BarrelBlockActor.h`），所以 02 里的向下转型对木桶合法、不是 UB；原地加了注释说明，免得以后被「修」错。
- **商品名退回第 4 行**：容器空了（卖光）时 `lookup.itemName` 是空串，表单改用牌面第 4 行的显示名，并显示 `可成交份数：0 份`——牌还是商店，不该因为卖光就变成可随意编辑的普通牌。

**已知边界**

- **店主身份在 06 落地前可被改牌抢走**：`ensureOwnerRecorded` 按既定决策「记录的 XUID ≠ 编辑者就写」，所以只要有人能成功提交一次正面编辑（引擎路径与 fallback 都一样），他就是店主了。DESIGN §11 要求的「转让也受店主权限约束」靠 06 实现（交互阶段拦下 + 提交阶段兜底取消），**在此之前是开启的**。12 验收时要先测 06 再测「店主能不能被抢」。
- **「这个箱子已经有商店了」的牌仍会弹表单**：`inspectShop` 的可用性判据是「附着格是允许的容器」，`AlreadyBound` 也满足（`ref` 有值）。两块牌争同一库存只能由外部变化造成（02 已记录），表单照弹、库存统计按各自商品算，P0 不仲裁。
- **`getSideFacingPlayer` 的扇区语义未实测**：它决定「玩家看到的是不是正面」，站立牌与吊牌在玩家贴着侧面时的返回值需要真机确认；最坏情况是所有交互都被判成背面（表单不弹，但也不会误伤），不会造成误拦。
- **冷却被刷穿的可能**：`kMaxTracked` 溢出时会清空整表，理论上有心人可以借此重置自己的冷却。代价是需要 250ms 内在 512 个不同的牌子坐标上各点一次，实际不可达；换成 LRU 驱逐会更严，但当前不值得。
- **配置改动要重启**：`shopInteractionCooldownMs` 在注册监听时读一次（与其它配置项一致，`/reload` 不重载配置）。

**审查后修正（code-review 两轴）**

- **`ensureOwnerRecorded` 的覆写条件保持为既定决策**（Spec 轴提出过改成「只补空串」）：审查担心的是「06 落地前任何人改牌就能抢店」，但这正是 06 的职责（DESIGN §9.2 交互阶段拦下 + 提交阶段兜底取消），而决策 1 写的就是「`mLastEditXuid != player.getXuid()` 即写」。按决策实现，风险写进上面的已知边界，不在这里改契约。
- **纯收购店不拿库存冒充可成交份数**（Spec 轴）：只写 `S` 的店，买家侧限额是「你手里的数量 / 店主资金」（§10.3），不是箱内库存，所以这一行改写成 `可成交份数：—（本店只收购，以你手里的数量为准）`，有 doctest 覆盖。
- **一次点击只解析一次正面文本**（Standards 轴）：`readShopOwner` 改成接收已算好的 `ShopSignResult`，不再自己重解析（原来一次点击最多 4 次）。
- **`250` 只留一处**（Standards 轴）：`TradeGate` 构造去掉了默认参数，毫秒数只在 `shopInteractionCooldownMs` 定义。
- **`Cell` ↔ `BlockPos` 只留一处**：`toBlockPos` / `toCell` 从匿名命名空间提升到 `ContainerFinderBds.h`，监听器不再手拼 `Cell{pos.x,pos.y,pos.z}`。
- **行号魔数收成枚举**：`ShopSign.h` 新增 `SignLine{OwnerLine, AmountLine, PriceLine, ItemLine}`，新代码不再写 `[0]` / `[3]`。
- **术语对齐 GLOSSARY**：注释里的「合并库存」改成「商店箱里（双箱合并）」，避开 `容器 _Avoid_: 库存`。
- **未采纳**：`TradeGate` 的字符串键（要额外引入 `Cell` 的 hash 组合，收益不抵代码量）；`signBlockActorAt` 单独拆文件（8 行函数，拆开只是换个地方漂）。

**只能真机验收的项（留给 12）**

1. **在 `PlayerInteractBlockEvent` 里 `cancel()` 是否真的阻止原版告示牌编辑界面弹出**（DESIGN §14「编辑拦截的用户体验」）。Plan B：用 iListenAttentively 的 `ReceivePacketBeforeEvent` / `SendPacketEvent` 拦截 `OpenSignPacket`，从服务端不发那个包。已写进 12 的待验清单。
2. **`getSideFacingPlayer` 的实际返回值**：正面/背面判定与扇区边界（贴着牌侧面、吊牌、站立牌各试一次）；看背面必须照常弹原版编辑界面。
3. **`mLastEditXuid` 的实际取值**：补写前它是不是空串；补写后是否真的持久化（重启后再点牌，店主是否仍是自己）。这条与 ADR-0005 的落地直接相关。
4. **`setMessageForServerScripingOnly` 的副作用**：不 `refresh` 时客户端是否真的不抖；它是否会顺带覆盖颜色/发光等格式字段（文本传的是 `getMessage()` 的原样内容，理论上不变）。
5. **表单回调线程**：本期按钮只发消息（`Player::sendMessage`），不碰方块；04/05 接真实交易时必须在此确认线程，回调若要动容器得切回主线程。
6. **`getAuxValue() + CompoundTag::equals` 的严格比较是否真能区分**：用附魔钻石剑与普通钻石剑混放，确认统计只算同一种。
7. **双箱合并统计**：54 格里两半都放商品，确认「可成交份数」是两边之和。
8. **真实商品名的显示宽度**：中文长名在牌面第 4 行被客户端裁掉，表单正文是否完整显示。
