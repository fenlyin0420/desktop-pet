#include "platform/dynlib.hpp"

#include <windows.h>

#include "win_utf.hpp"

namespace fx {

bool DynLib::open(const std::string& path, std::string* error) {
    close();
    const std::wstring wide = platform::utf8ToWide(path);
    HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        if (error) {
            *error = "无法加载动态库: " + path + "（GetLastError=" + std::to_string(GetLastError()) + "）";
        }
        return false;
    }
    handle_ = reinterpret_cast<void*>(module);
    path_ = path;
    return true;
}

void* DynLib::sym(const char* name) const {
    if (!handle_) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
}

void DynLib::close() {
    if (handle_) FreeLibrary(static_cast<HMODULE>(handle_));
    handle_ = nullptr;
    path_.clear();
}

}  // namespace fx
