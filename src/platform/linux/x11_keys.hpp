// X11 keysym → Windows VK 码；X11 修饰键状态 → Modifier 位。
#pragma once

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdint>

#include "platform/window.hpp"

namespace fx {

inline int keysymToVk(KeySym ks, int keycode) {
    // 字母/数字：VK 与 ASCII 大写一致
    if (ks >= 'a' && ks <= 'z') return int(ks - 'a' + 'A');
    if (ks >= 'A' && ks <= 'Z') return int(ks);
    if (ks >= '0' && ks <= '9') return int(ks);
    switch (ks) {
        case XK_BackSpace: return 0x08;
        case XK_Tab: return 0x09;
        case XK_Return: case XK_KP_Enter: return 0x0D;
        case XK_Pause: return 0x13;
        case XK_Caps_Lock: return 0x14;
        case XK_Escape: return 0x1B;
        case XK_space: return 0x20;
        case XK_Page_Up: return 0x21;
        case XK_Page_Down: return 0x22;
        case XK_End: return 0x23;
        case XK_Home: return 0x24;
        case XK_Left: return 0x25;
        case XK_Up: return 0x26;
        case XK_Right: return 0x27;
        case XK_Down: return 0x28;
        case XK_Print: return 0x2C;
        case XK_Insert: return 0x2D;
        case XK_Delete: return 0x2E;
        case XK_Shift_L: case XK_Shift_R: return 0x10;
        case XK_Control_L: case XK_Control_R: return 0x11;
        case XK_Alt_L: case XK_Alt_R: return 0x12;
        case XK_Super_L: case XK_Super_R: return 0x5B;
        case XK_Menu: return 0x5D;
        case XK_Num_Lock: return 0x90;
        case XK_Scroll_Lock: return 0x91;
        case XK_semicolon: return 0xBA;
        case XK_equal: return 0xBB;
        case XK_comma: return 0xBC;
        case XK_minus: return 0xBD;
        case XK_period: return 0xBE;
        case XK_slash: return 0xBF;
        case XK_grave: return 0xC0;
        case XK_bracketleft: return 0xDB;
        case XK_backslash: return 0xDC;
        case XK_bracketright: return 0xDD;
        case XK_apostrophe: return 0xDE;
        default: break;
    }
    if (ks >= XK_F1 && ks <= XK_F12) return 0x70 + int(ks - XK_F1);
    if (ks >= XK_KP_0 && ks <= XK_KP_9) return 0x60 + int(ks - XK_KP_0);
    return keycode;  // 未识别：退回 X keycode（配置里也可用 keycode 指定）
}

inline uint32_t modsFromState(unsigned state) {
    uint32_t mods = 0;
    if (state & ControlMask) mods |= ModCtrl;
    if (state & ShiftMask) mods |= ModShift;
    if (state & Mod1Mask) mods |= ModAlt;
    if (state & Mod4Mask) mods |= ModSuper;
    return mods;
}

inline int mapX11Button(int button) {
    // X11 侧键是 8/9，配置里的侧键沿用 Windows 的 5/6
    if (button == 8) return 5;
    if (button == 9) return 6;
    if (button == 10) return 7;
    return button;
}

}  // namespace fx
