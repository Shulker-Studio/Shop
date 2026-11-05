#pragma once

#include "core/sign/ContainerFinder.h"

#include <optional>

namespace ss::shop {

/// 基岩 `facing_direction` 的取值（也是 `Direction` 的索引顺序）：
/// 0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东。
///
/// 这几个数字只在这个头文件里出现一次；别处一律用 `cellInDirection` / `cellAbove` 换格子，
/// 免得「北是 z-1」这类事实散落到各条 hook 里。
enum class FacingDirection : int {
    Down  = 0,
    Up    = 1,
    North = 2,
    South = 3,
    West  = 4,
    East  = 5,
};

/// 装置朝向 → 它要打交道的那一格容器（DESIGN §6.2 H2a / H2d）。
///
/// 漏斗推出与投掷器都靠它把方块状态 `facing_direction` 换成目标格。
/// 越界方向返回空（调用方放行）：脏方块状态不该让一次搬运被误拦，
/// 也不该被我们猜成某个方向。
/// 偏移量取自 `neighboursOf`，所以「上/下/北/南/西/东」与坐标的对应仍然只有一处定义。
std::optional<Cell> cellInDirection(Cell device, int facingDirection);

/// 漏斗抽取的源容器：漏斗正上方那一格。
///
/// `Hopper::_tryPullInItemsFromAboveContainer` 只拿到自己的位置，没有方向参数——
/// 方块漏斗与漏斗矿车都固定从上方抽，所以这里连方向都不用问。
Cell cellAbove(Cell device);

/// 这一次搬运要不要跳过（DESIGN §6.2 H2a / H2c / H2d、§9.2）。
///
/// **四条搬运路径共用这一个判据**（与票面同一分法：漏斗方块抽/塞、漏斗矿车吸、
/// 铜傀儡取放、投掷器塞）。规则只有一句——目标容器是商店箱就跳过**这一次**搬运，
/// 与谁在搬、搬进还是搬出、店主是谁都无关：§9.2 表格里那三行对非店主、店主、OP
/// 都是「阻止」，商店箱的内容只能通过交易流动，店主也不能拿漏斗替自己开箱。
///
/// 粒度是「这一次搬运」，不是「停掉这个装置」：漏斗与投掷器下一次照常搬运别处，
/// 铜傀儡也不因此失去其他行为（§6.2 H2c 关于 goal 粒度的讨论）。
///
/// 入参就是适配层 `isShopBlockAt` 的答案——判据也只在那里组装一次，
/// 这个函数负责的是「判出来之后怎么办」，将来若有例外（配置开关之类）也只改这一处。
/// 普通容器、判定失败、读不到牌面一律返回 `false`：不误伤普通箱子。
bool shouldSkipContentTransfer(bool targetIsShopContainer);

} // namespace ss::shop
