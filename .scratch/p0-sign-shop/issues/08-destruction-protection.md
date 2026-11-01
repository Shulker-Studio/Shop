# 08: 破坏防护（方块级）

**What to build:** 爆炸、活塞、凋灵、火焰对商店箱无效，而旁边的一切照常。这是刻意偏离 Java 版的地方：不是取消整场爆炸，只是让商店箱从破坏目标里消失。

外加一条容易被忽略的路径：非店主拆掉商店牌依附的那块方块，会让牌子物理掉落、商店消失——等于绕过了"不能拆牌"。

**Blocked by:** 02

**Status:** resolved

- [x] TNT 在商店箱旁爆炸 → 商店箱幸存，旁边方块正常被炸毁
- [x] 活塞推商店箱 → 推不动；推旁边的方块 → 正常推动
- [x] 活塞拉动商店箱 → 同样失败
- [x] 凋灵破坏范围内含商店箱 → 商店箱不受影响，其他方块正常被破坏
- [x] 火焰烧到商店箱 → 不燃烧
- [x] 非店主拆掉商店牌背后那块墙 → 拆不掉，牌子不掉落
- [x] 店主拆掉那块墙 → 可以拆，牌子随之掉落，商店消失（与拆牌等价）
- [x] 以上事件对普通箱子照常生效

## Answer

**已实现（`xmake build Shop` / `ShopTests` 通过，`xmake test` 176 cases / 2736 assertions 全绿）**

- `src/shop/DestructionProtection.{h,cpp}`（新，纯逻辑，不含引擎类型）：
  - `DestructionTarget{pos, shopBlock}` + `cellsToSpare(targets)`：从引擎给的「要毁掉的格子」里挑出**要放过**的（ADR-0004 的「剔除 ≠ 取消」只写在这里）。爆炸拿它筛 `mBlocks`，凋灵/火焰拿它决定跳过哪一格。
  - `signsAttachedTo(target, signs)`：哪些牌的**附着格**正好是目标格（站立看下方、吊牌看上方、墙牌看向背面的反方向）。几何口径仍然只有 `attachedCellOfSign` 一处。
- `src/shop/ContainerProtection.{h,cpp}`：`ShopBlockTouch` 新增 `BreakSignSupport`（拆商店牌依附的方块）。放行条件与拆牌相同（`canManageShop`：店主与 OP），提示单独一句——必须点出后果（「拆了牌子会掉下来、商店也就没了」），否则玩家以为自己在拆一块普通方块；管理员商店与无主牌都只提 OP。
- `src/mod/ContainerFinderBds.{h,cpp}`：
  - `isShopBlockAt(region, pos, config)`：**判据只在这一次组装**（§9.3：牌面「像商店」+ 附着容器），四条 hook 与拆除路径共用；带一道「有没有方块实体」的粗筛（商店牌与容器必有，爆炸列表里大部分格子一句就挡掉）。
  - `shopSignsWithAttachedCell(region, 附着格, config)`：与 07 的 `attachedShopSignsAt` 互为两个口径——那个问「这一格**容器**绑着哪些牌」，这个只问「哪些牌的附着格是这一格」，不管这一格是不是容器。
- `src/mod/DestructionProtectionHooks.{h,cpp}`（新，5 条 hook，手动注册/反注册，逐条记账，一条符号漂移不拖垮其他四条）：
  - H3a `BlockEventCoordinator::sendEvent`：只处理 `ExplosionStartedEvent` 那一支（同一个 coordinator 还发别的方块事件），把商店方块从 `mBlocks` 里删掉再放行。
  - H3b `PistonBlockActor::_attachedBlockWalker`：路径上遇到商店方块返回 false。
  - H3c `WitherBoss::_destroyBlocks`（只置线程局部标志后放行原版）+ `Level::$destroyBlock`（标志期内对商店方块返回 false）。
  - H3d `FireBlock::checkBurn`：跳过被烧的那一格。
- `src/mod/ContainerProtectionListener.{h,cpp}`：H3e 接在 07 **已有的** `PlayerAttackBlockBeforeEvent` / `PlayerDestroyBlockEvent` 监听里（不给同一事件注册第二个监听）：先判「这一格是不是商店方块」，不是的话再判「这一格上挂着谁的商店牌」。放行时什么都不做——牌子由原版物理打落，与拆牌等价。
- `Shop.cpp`：`enable` 注册 5 条 hook、`disable` 反注册（与三组监听同序）。
- `tests/shop/DestructionProtectionTest.cpp`（新，7 个中文用例）：放过名单（只放过受保护的、保持引擎顺序、旁边全普通时一个不放过）+ 依附几何（站立/吊/墙牌四朝向/只挑挂在这一格的）。
- `tests/shop/ContainerProtectionTest.cpp`（+4）：`BreakSignSupport` 的放行条件与三种提示文案。

**决策与偏离**

- **不照搬 ChestLock 的凋灵提前返回**。26.51.8 的 `_destroyBlocks` 一次只拿到整个 `AABB`，提前返回 = 取消那一次破坏的全部方块，旁边的一块也不掉，直接违反 ADR-0004 与票面第 4 条。改成「`_destroyBlocks` 只置一个线程局部标志后放行原版，`Level::$destroyBlock` 在标志期内逐格拒绝」。标志只在这一次凋灵破坏期间为真，所以爆炸、玩家拆除、所有权判定、以及 07 放行后我们主动打商店牌的那次 `destroyBlock` 都不受影响。ADR-0004 已补「修订（08 落地时）」一节把原句与落点对齐。
- **H3d 用 `FireBlock::checkBurn`，不用 iListenAttentively 的 `FireTryBurnBlockBeforeEvent`**：那个事件只能取消整场燃烧，粒度不对（§6.2 的理由对火焰同样成立）。
- **活塞只有「整次推不动」**：原版没有部分推动语义，所以一次推动里含商店方块时整次失败——ADR-0004 要求的是「不取消活塞本身」，不是「拆开一次推动」。推与拉共用 `_attachedBlockWalker`，一条 hook 覆盖两边。
- **H3e 挂在同一判据上**：拆依附方块的放行条件与拆牌一样是 `canManageShop`，所以走同一张 `planShopBlockTouch` 表，只有说法不同（`BreakSignSupport`）。攻击路径同样接（体验：不用徒劳地挖），拆除路径是权威兜底。
- **判据不看 `shopContainers` 白名单、牌面用「像商店」**：与 07 完全一致，改配置/脏数据不该让商店方块失去保护。

**已知边界**

- **凋灵逐格剔除依赖一处引擎路径假设**：凋灵销毁方块必须经过 `Level::destroyBlock`。这一条无法单测（不给 hook 写假装验证的单测），只能真机验收；若守卫一次都没命中，退路是回到提前返回并接受「凋灵范围内旁的方块不被破坏」。
- **店主拆依附方块时牌子靠原版物理掉落**：我们只放行，不自己打牌（自己打会与物理重复掉落）。若真机发现牌子不掉，则「与拆牌等价」不成立，需要改成放行时显式 `Level::destroyBlock` 打牌。
- **爆炸/活塞不保护「商店牌依附的方块」**：票面第 1、2 条只要求商店箱/商店牌本身幸存，H3e 也只点名玩家拆除路径。所以用 TNT 炸掉牌子背后那块石头、或用活塞推走它，牌子仍会掉落、商店仍会消失——这是与拆依附方块同一类绕过，但不在本票范围。P1 若收紧，做法是把 `cellsToSpare` 的判定从「这一格是商店方块」扩成「这一格是商店方块或是商店牌的附着格」。
- **容器口径沿用 07**：`isShopBlockAt` 的容器分支只认 chest / barrel（`attachedShopSignsAt` 的实现范围），其他容器（漏斗、潜影盒…）旁的商店牌不保护那个容器本身。
- **玩家每拆一格多一次 6 邻居扫描**：`PlayerAttackBlockBeforeEvent` / `PlayerDestroyBlockEvent` 对**任意**方块都多判一次依附方块（放行分支）；与 07 的 12 邻居反查同量级，只在这些低频事件里发生。
- **`checkBurn` 的 `pos` 语义按头文件推断**（候选被火焰烧的那一格），未在真机核对；若语义是火焰自身那一格，表现为「火不蔓延」，验收项里会看出来。

**指标**

- `xmake build Shop`：`Shop.dll` 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `xmake test`：176 cases / 2736 assertions 全绿（07 之后 165 / 2694）。
- `git diff --check`：无空白问题。

**只能真机验收的项（已转 12）**

1. TNT 爆炸：商店箱与牌幸存、旁边照炸、同一场里的普通箱子照炸。
2. 活塞推 / 拉商店箱与商店牌都失败，推旁边普通方块正常。
3. 凋灵破坏范围内商店方块幸存、其他方块照毁（本票唯一「实现方式与参考实现不同」的点）。
4. 火焰不烧商店方块、周围照烧。
5. 非店主拆依附方块被拦（左键 + 破坏两条路径），店主拆时牌子真的物理掉落、商店消失。
6. 依附判定的边界：立在箱子顶面的牌、吊牌的天花板、普通告示牌的依附方块不受影响。
7. 配置非法（模组未启用）时五条 hook 对普通方块完全无影响。
