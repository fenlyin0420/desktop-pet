#include "file_io.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace fx {
namespace {

std::filesystem::path toPath(const std::string& utf8) {
    // C++17：u8path 会把 UTF-8 正确转成 Windows 宽路径
    return std::filesystem::u8path(utf8);
}

}  // namespace

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    out.clear();
    std::ifstream in(toPath(path), std::ios::binary);
    if (!in) {
        if (error) *error = "无法打开文件: " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        if (error) *error = "无法读取文件大小: " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in && !in.eof()) {
        if (error) *error = "读取文件失败: " + path;
        return false;
    }
    return true;
}

bool writeFile(const std::string& path, const void* data, size_t size, std::string* error) {
    std::ofstream out(toPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "无法写入文件: " + path;
        return false;
    }
    if (size > 0) out.write(static_cast<const char*>(data), std::streamsize(size));
    if (!out) {
        if (error) *error = "写入文件失败: " + path;
        return false;
    }
    return true;
}

bool appendFile(const std::string& path, const std::string& text, std::string* error) {
    std::ofstream out(toPath(path), std::ios::binary | std::ios::app);
    if (!out) {
        if (error) *error = "无法追加文件: " + path;
        return false;
    }
    out.write(text.data(), std::streamsize(text.size()));
    if (!out) {
        if (error) *error = "追加文件失败: " + path;
        return false;
    }
    return true;
}

}  // namespace fx
