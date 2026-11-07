// 动态库加载：Linux dlopen，Windows LoadLibraryW。
#pragma once

#include <string>

namespace fx {

class DynLib {
public:
    DynLib() = default;
    ~DynLib() { close(); }
    DynLib(const DynLib&) = delete;
    DynLib& operator=(const DynLib&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);
    void* sym(const char* name) const;
    void close();

    bool valid() const { return handle_ != nullptr; }
    void* handle() const { return handle_; }
    const std::string& path() const { return path_; }

private:
    void* handle_ = nullptr;
    std::string path_;
};

}  // namespace fx
