#include "menu.hpp"

namespace fx {

void Menu::open(float x, float y, bool locked) {
    visible_ = true;
    x_ = x;
    y_ = y;
    hover_ = -1;
    items_ = {
        {locked ? "解除锁定  (Ctrl+Shift+T)" : "锁定 / 穿透 (Ctrl+Shift+T)", MenuAction::ToggleLock},
        {"隐藏桌宠  (Ctrl+Shift+H)", MenuAction::Hide},
        {"恢复默认大小", MenuAction::ResetSize},
        {"重新加载模型", MenuAction::Reload},
        {"退出", MenuAction::Quit},
    };
}

int Menu::hitTest(float x, float y) const {
    if (!visible_) return -1;
    if (x < x_ || x > x_ + width()) return -1;
    const float top = y_ + titleHeight();
    if (y < top) return -1;
    const int idx = int((y - top) / itemHeight());
    if (idx < 0 || idx >= int(items_.size())) return -1;
    return idx;
}

int Menu::actionAt(int index) const {
    if (index < 0 || index >= int(items_.size())) return MenuAction::NoAction;
    return items_[size_t(index)].action;
}

bool Menu::contains(float x, float y) const {
    if (!visible_) return false;
    return x >= x_ && x <= x_ + width() && y >= y_ && y <= y_ + titleHeight() + itemHeight() * float(items_.size());
}

void Menu::setHover(float x, float y) { hover_ = hitTest(x, y); }

void Menu::render(GLContext& gl, TextRenderer& text) {
    if (!visible_) return;
    const float w = width();
    const float h = titleHeight() + itemHeight() * float(items_.size());

    // 底色（预乘 alpha）
    Color bg{0.08f, 0.09f, 0.12f, 0.94f};
    gl.setTint(bg);
    gl.fillRect(x_, y_, w, h, bg);
    gl.setTint(Color{});
    // 标题栏
    Color bar{0.86f, 0.32f, 0.44f, 0.96f};
    gl.fillRect(x_, y_, w, titleHeight(), bar);
    gl.fillRect(x_, y_ + titleHeight() - 1.0f, w, 1.0f, Color{1, 1, 1, 0.12f});

    text.draw(gl, "绯雪 · Linux 版", x_ + 12.0f, y_ + 7.0f, 14.0f, Color{1, 1, 1, 1});

    for (size_t i = 0; i < items_.size(); ++i) {
        const float iy = y_ + titleHeight() + itemHeight() * float(i);
        if (int(i) == hover_) {
            gl.fillRect(x_ + 4.0f, iy + 2.0f, w - 8.0f, itemHeight() - 4.0f, Color{1, 1, 1, 0.13f});
        }
        text.draw(gl, items_[i].label, x_ + 14.0f, iy + 6.0f, 14.0f, Color{1, 1, 1, 0.94f});
    }
}

}  // namespace fx
