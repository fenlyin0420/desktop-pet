// 窗口内右键菜单（跨平台，不依赖系统托盘）。
#pragma once

#include <string>
#include <vector>

#include "gl.hpp"
#include "text.hpp"

namespace fx {

struct MenuAction {
    enum Id { NoAction = 0, ToggleLock, Hide, ResetSize, Reload, Quit };
};

class Menu {
public:
    void open(float x, float y, bool locked, float maxW = 0.0f, float maxH = 0.0f);
    void close() { visible_ = false; }
    bool visible() const { return visible_; }

    void setHover(float x, float y);
    int hovered() const { return hover_; }
    // 命中测试：返回条目下标，未命中返回 -1
    int hitTest(float x, float y) const;
    int actionAt(int index) const;
    bool contains(float x, float y) const;

    void render(GLContext& gl, TextRenderer& text);

private:
    struct Item {
        std::string label;
        int action = MenuAction::NoAction;
    };
    float itemHeight() const { return 32.0f; }
    float width() const { return 244.0f; }
    float height() const { return titleHeight() + itemHeight() * float(items_.size()); }
    float titleHeight() const { return 30.0f; }

    bool visible_ = false;
    float x_ = 0, y_ = 0;
    int hover_ = -1;
    std::vector<Item> items_;
};

}  // namespace fx
