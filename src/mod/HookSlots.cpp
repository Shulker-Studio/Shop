#include "mod/HookSlots.h"

#include "mod/Shop.h"

#include <iterator>

namespace ss::shop {

void attachHooks(std::span<HookSlot> slots) {
    for (HookSlot& slot : slots) {
        // 重复注册 = 同一条路径被拦两次；已经挂上的不重复挂（enable 不该被调两次，这里只是幂等）
        if (slot.attached) {
            continue;
        }
        int const status = slot.attach();
        slot.lastReturn = status; // 只给 `/shop status` 看，不拿来判成败
        if (status != 0) {
            Shop::getInstance().getSelf().getLogger().warn(
                "hook 注册返回 {}（{}），若对应行为无效即为符号漂移",
                status,
                slot.name
            );
        }
        slot.attached = true;
    }
}

void detachHooks(std::span<HookSlot> slots) {
    for (auto slot = std::rbegin(slots); slot != std::rend(slots); ++slot) {
        if (!slot->attached) {
            continue;
        }
        slot->detach();
        slot->attached = false;
    }
}

} // namespace ss::shop
