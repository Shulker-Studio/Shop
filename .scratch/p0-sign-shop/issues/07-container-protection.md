# 07: 容器防护

**What to build:** 非店主打不开商店箱、拆不掉商店箱、拆不掉商店牌。店主拆牌即关店，箱子立刻恢复成普通箱子。双箱作为整体处理：拆掉一半，商店失效。

**Blocked by:** 02

**Status:** resolved

- [x] 非店主右键商店箱 → 打不开
- [x] 非店主拆商店箱 → 拆不掉
- [x] 非店主拆商店牌 → 拆不掉
- [x] 店主拆牌 → 成功，箱子恢复普通箱子
- [x] 店主开箱（配置允许时）→ 可以打开
- [x] 拆掉双箱的一半 → 商店失效，牌子随之物理掉落
- [x] 普通箱子与其他普通告示牌不受影响

## Answer

**已实现（`xmake build Shop` 通过，`ShopTests` 165 cases / 2694 assertions 全绿，`xmake test` 通过）**

- `src/shop/ContainerProtection.{h,cpp}`（新，纯逻辑，不含引擎类型）：
  - `planShopBlockTouch({touch, actor, allowOwnerOpenChest, ownerName, adminShop})`，`touch` 是 `OpenChest / BreakChest / BreakSign`。三种操作放行条件不同，所以做成一个枚举的三个取值而不是几个 bool。
  - 开箱：OP 恒可；店主看 `allowOwnerOpenChest`；无主牌（`NoRecordedOwner`）只给 OP。
  - 拆箱 / 拆牌：`canManageShop`（店主与 OP）——「管理身份」仍然只有 `SignProtection.h` 那一处口径。
  - 提示文案也在这里：非店主、管理员商店（只提 OP）、无主牌（只提 OP）、店主被配置挡下（点出 `allowOwnerOpenChest`）各一句；`allowed == true` 时消息为空。
- `src/mod/ContainerFinderBds.{h,cpp}`：新增反查
  - `attachedShopSignsAt(region, 容器坐标, config)`：这一格容器被哪些商店牌绑着。单箱看自己六面，双箱连另一半六面一起看；同一块牌同时是两半的邻居时只收一次。返回空 = 普通箱子 / 不是容器。`AttachedShopSign` 带上几何（拆牌坐标）与店主。
  - `shopSignOwnerAt(region, 牌子坐标, config)`：这一格告示牌是不是商店牌，是的话带上店主。
  - 两个判据都与 06 的牌面防护对齐：**牌面用 `looksLikeShopSign` 而不是 `ok()`**，且**不看 `shopContainers` 白名单**。改配置、调小 `maxShopAmount`、外部工具写脏数据，都不该让商店箱失去保护（DESIGN §9.2 / §9.3）。
  - 「扫两半 × 六面」收成 `forEachSignAround`（开店时的冲突检查与保护时的反查共用）；「牌子 → 店主」收成 `shopSignOwnerHere`；「容器两半 + `ContainerRef`」收成 `containerAt`（`attachedContainerOf` 与反查共用）。
- `src/mod/ContainerProtectionListener.{h,cpp}`（新）：
  - `PlayerOpenContainerBeforeEvent`：命中商店箱且判定不放行 → `cancel()` + 一句提示。
  - `PlayerAttackBlockBeforeEvent`：在挖掘开始前拦下（牌子优先判，告示牌不是容器，两个分支不会同时命中）。
  - `PlayerDestroyBlockEvent`：权威兜底，再判一次。放行拆商店箱时，**先按原版语义把商店牌打掉（有掉落）**，再让这一格继续被拆。
- `Shop.cpp`：`enable` 注册、`disable` 反注册（与另外两组监听同序）。
- `tests/shop/ContainerProtectionTest.cpp`（新）：开箱/拆箱/拆牌的权限与提示文案，13 个中文用例。

**决策与偏离**

- **拆商店箱时显式打掉商店牌**。牌子即数据源（ADR-0002），本来「拆牌即关店」不需要清理；但双箱拆掉**不**挨着牌子的那一半时，另一半还在、牌子不会因失去支撑而自己掉落，商店会带着半份库存继续营业。所以拆商店箱的放行分支里先 `Level::destroyBlock(region, 牌位, dropResources=true, ActorChangeContext{玩家})`，两半都走同一条路径——挨着牌子的那一半也由我们打掉，原版就不会再掉一次。做了碰撞张数的实机确认项（见 12）。
- **无主牌在容器操作上 fail-closed（只给 OP）**，与牌面文本的 fail-open 故意不同：文本是牌子自己的事，开箱/拆箱/拆牌动的是库存与方块本身；查不到主人不该等于「谁都能搬」。DESIGN §9.2 那张口径表已补一行。
- **配置关掉的是「店主开箱」，不是「店主拆箱」**。拆掉自己的箱子本来就是店主的权利（拆了即关店）。`allowOwnerOpenChest = false` 时店主仍可拆。
- **容器保护不看 `shopContainers` 白名单**。白名单决定「能不能开店」，不决定「已有的店要不要保护」；否则服主改一次配置就等于把所有商店箱解锁。
- **攻击与拆除两道事件都接**：攻击负责在挖掘开始前拦下（体验），拆除负责兜底（别的路径绕过攻击也拆不掉）。与 DESIGN §9.2 表格一致。

**已知边界**

- **「贴一块牌就占住别人的普通箱子」仍然可能**：保护从牌子推导（ADR-0002），P0 没有「这个箱子原来是谁的」这个信息，所以任何人给一个尚未绑定的箱子贴一块合法商店牌，就能把它变成自己的商店箱，原主人从此开不了（只有 OP 能拆）。这是 ADR-0002 的固有代价，不是本票引入的；P1 可考虑开店收费/权限或与领地插件配合，已在此记录。
- **被拦下的攻击提示不节流**：按住左键会刷屏。攻击路径不吃交互冷却（同 06 的 `Block` 路径理由：冷却会吞掉紧接着的那次点击）。
- **`Level::destroyBlock` 在 `PlayerDestroyBlockEvent` 内部调用的重入/重复掉落**只能在真机确认；若发现双掉，退路是改成延迟一拍（下一 tick）再打牌。
- **反查是每次事件扫 12 个邻居**（双箱两半各 6 面），无缓存（ADR-0002）。只在开箱/攻击/拆除这些低频事件里发生。
- **牌面「像商店」的宽口径会连带保护非法牌**：例如第 2 行写坏了的商店牌仍然让箱子受保护（与 06 的「拆箱子也不能关掉牌面保护」同源）。代价是一个写坏的店要店主自己修好或拆掉，OP 可代拆。
- **提交阶段没有第二道闸**：开箱/拆除只有事件这一道（引擎没有「已开箱」「已拆除」的提交事件可拦），真机验收是唯一验证手段。

**指标**

- `xmake build Shop`：`Shop.dll` 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `build/windows/x64/release/ShopTests.exe`：165 cases / 2694 assertions 全绿（06 之后 152 / 2652，只增不减）。
- `xmake test`：通过。
- `git diff --check`：无空白问题。

**只能真机验收的项（已转 12）**

1. `PlayerOpenContainerBeforeEvent` 的 `cancel()` 是否真的不开箱（单箱 + 双箱两半 + 木桶配置）。
2. `allowOwnerOpenChest` 两种取值下店主/OP 的实际表现。
3. `PlayerAttackBlockBeforeEvent` 的取消是否停止挖掘进度；`PlayerDestroyBlockEvent` 的取消是否真的保住方块（生存 + 创造）。
4. 双箱拆一半（挨着牌子/不挨着牌子各一次）→ 牌子只掉一次、剩下的箱子恢复普通。
5. `Level::destroyBlock(..., ActorChangeContext{玩家})` 在事件内部调用的重入、掉落与双箱物理是否都正常。
6. `attachedShopSignsAt` 在「双箱尚未配对」「牌子贴在接缝上方」等边角形态下是否仍只返回一次。
