#include "platform/paths.hpp"

#include <fontconfig/fontconfig.h>

#include <string>

namespace fx {
namespace platform {

std::string findUiFont() {
    const char* families[] = {"Noto Sans CJK SC", "Noto Sans SC", "Source Han Sans SC", "WenQuanYi Micro Hei",
                              "Noto Sans CJK TC", "Droid Sans Fallback", "DejaVu Sans", nullptr};
    FcInit();
    for (int i = 0; families[i]; ++i) {
        FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(families[i]));
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res = FcResultNoMatch;
        FcPattern* match = FcFontMatch(nullptr, pat, &res);
        FcPatternDestroy(pat);
        if (!match) continue;
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            std::string path(reinterpret_cast<char*>(file));
            FcPatternDestroy(match);
            return path;
        }
        FcPatternDestroy(match);
    }
    return std::string();
}

}  // namespace platform
}  // namespace fx
