# 02: 写牌子就开店

**What to build:** 在真实服务器里写完一块牌子，服务器把第 1 行换成编辑者当前的名字、第 4 行换成绑定容器里第一个非空槽位的物品名，然后把规范化后的四行写回牌子。写得不对时只提示原因并保留玩家的牌子。牌子成功连上容器后成为商店。

同时确立绑定规则：牌子必须**附着**在容器上——墙牌看背面、站立牌看下方、悬挂牌看悬挂点，只判断这一格是不是容器，不扫邻居。双箱合并成一个库存；一个容器同时只能被一块牌绑定。

**Blocked by:** 01

**Status:** resolved

- [x] 写合法四行 → 第 1 行与第 4 行被服务器覆盖成正确内容
- [x] 第 3 行没有 `B` 或 `S` → 完全不处理，牌子原样保留
- [x] 份量非法 → 收到提示，牌子保留原文本
- [x] 容器为空 → 提示先放商品，不创建
- [x] 墙牌贴在箱子侧面 → 成为商店；站立牌立在箱子顶面 → 成为商店
- [x] 站立牌立在箱子旁边的地上 → 不是商店，提示"牌子要贴在或立在箱子上"
- [x] 墙牌贴在箱子旁边的墙上 → 不是商店，同样提示
- [x] 牌子没有附着容器 → 提示并保留
- [x] 容器已被另一块牌绑定 → 拒绝，提示"这个箱子已经有商店了"
- [x] 背面写商店格式 → 不生效
- [x] 双箱：牌子只贴在一半上 → 可交易全部 54 格
- [x] 双箱：两半各挂一块牌 → 第二块被拒（合并后是同一个库存）

## Answer

**已实现（`xmake` 构建通过，`xmake test` 45 cases / 213 assertions 全绿）**

- `src/shop/ContainerFinder.{h,cpp}`：纯逻辑。`attachedCellOfSign`（墙牌看背面、站立看下方、吊牌看上方）、`bindsTo`（双箱两半同一库存）、`isBoundByAnotherShopSign`（哪些牌算占用了容器）、`neighboursOf`（找容器时扫容器自己的六面）。
- `src/shop/ShopSign.*`：新增 `splitSignText`（不足补空、多余丢弃）与 `parseShopSignText`（整段文本 → 解析结果，省掉每个调用点自己拼 `array<string_view,4>`）。
- `src/mod/Config.*`：`ll::config::loadConfig` + 校验；非法配置打错误日志并让 `Shop::load()` 返回 false（拒绝启用，不带半份配置运行）。
- `src/mod/ContainerFinderBds.*`：BDS 适配。方块类型名后缀判形态（`standing_sign` / `wall_sign` / `hanging_sign`），`facing_direction`（退化 `cardinal_direction`）判朝向，状态名按 `minecraft:` 前缀兼容比对；附着格必须是 `Chest`/`BarrelBlock` 方块实体**且**在 `shopContainers` 白名单里；双箱用 `mLargeChestPaired` / `mLargeChestPairedPosition` 合并成 54 格；把容器六面（含另一半）的牌收集成 `NeighbourSign` 交给纯函数判冲突；商品名取合并库存第一个非空槽位（附着那一半优先）的 `ItemStack::getName()`。
- `src/mod/ShopService.*`：`planSignEdit` 只做决策（不写方块、不发消息、不取消事件）。`NotAShopSign` 原样放行；像商店但非法 → `Rejected` + 中文原因；合法 → `Normalized` 四行文本。管理员店第 1 行写配置名，普通店写 `editor.getNameTag()`。
- `src/mod/SignEditListener.cpp`：只处理 `SignTextSide::Front`；`Rejected` → `event.cancel()` + 消息；`Normalized` → 改 `event.text()`（改包而不是事后写回）。

**决策与偏离**

- **牌面文本的换行符**：基岩版 `FrontText["Text"]` 里四行之间是**真换行符**，不是字面反斜杠加 n 两字符（DESIGN §5.4 原文有误，已改；01 的 Answer 里"编辑包用字面反斜杠 n"也据此作废）。
- **侧挂的悬挂牌 P0 不支持**：基岩把吊在底面与挂在侧面做成同一个方块 ID，侧挂时 `facing_direction` 与墙面垂直，方块状态里没有"挂在哪面墙"的信息（PowerNukkit `BlockHangingSign` 也只能扫四周）；猜错会把牌子绑到旁边的另一个箱子上。只认 `hanging == true`（吊在方块底面），`hanging` 读不到时同样不猜。DESIGN §7.1 已同步。
- **编辑非法内容 = 包被丢弃，旧商店保持原样**（对齐 §4.2）。DESIGN §13 原写"旧商店失效"是 Java 版行为，已改。
- **第 1 行仍要求非空**（沿用 01 的解析规则，DESIGN §5.1.1）。服务器本来就会覆盖第 1 行，所以这条是纯 UX 摩擦；提示文案已解释"随便写点什么"。
- **管理员店第 1 行保留配置名**：`parseShopSign` 已经算出 `adminShop`（OP + 配置名 = 管理员店），规范化时不覆盖成 OP 的名字。管理员店的管理与保护仍属 11。

**已知边界**

- 这个容器已经有商店时，去编辑**自己那块**牌不会被自己挡住（按牌子坐标排除）。但若两块牌都合法地绑了同一个库存（只能由"双箱事后才配对成功"这类外部变化造成），两边都会收到"这个箱子已经有商店了"，需要人工拆掉一块。P0 不做自动仲裁。
- 双箱冲突依赖 `mLargeChestPaired` 已配对；配对未完成时按 27 格单箱处理。

**只能真机验收的项（留给 12）**

1. `facing_direction` 按状态名在 `BlockType::mStates` 里查得到、值就是 2/3/4/5（语义已用 PowerNukkit `BlockWallSign::onUpdate` 用 `getBlockFace().getOpposite()` 找支撑方块反证）。
2. 告示牌编辑时 `mLargeChestPaired` 已经配对好。
3. `ItemStack::getName()` 给出的商品名在告示牌上的实际显示宽度（超宽由客户端裁剪，P0 不做截断）。
4. **包路径是否写入 `mLastEditXuid`**：`setMessageForServerScripingOnly(side, text, ownerID)` 需要显式传 `ownerID`，说明 owner 不来自包。若引擎不写，ADR-0005 的店主判定要在 03/06 里补一次显式写入。
5. `ll::config::loadConfig` 对 `ShopConfig`（含 `std::vector<std::string>`）的反射往返。
