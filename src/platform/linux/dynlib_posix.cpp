#include "platform/dynlib.hpp"

#include <dlfcn.h>

namespace fx {

bool DynLib::open(const std::string& path, std::string* error) {
    close();
    // RTLD_LAZY：live2d-py 轮子里的 Core 带着未解析的 CPython 符号，NOW 会直接失败
    void* h = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        if (error) {
            const char* reason = dlerror();
            *error = "无法加载动态库: " + path + (reason ? std::string("（") + reason + "）" : std::string());
        }
        return false;
    }
    handle_ = h;
    path_ = path;
    return true;
}

void* DynLib::sym(const char* name) const {
    if (!handle_) return nullptr;
    return dlsym(handle_, name);
}

void DynLib::close() {
    if (handle_) dlclose(handle_);
    handle_ = nullptr;
    path_.clear();
}

}  // namespace fx
