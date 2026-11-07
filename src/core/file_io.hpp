// 跨平台文件读写：路径用 UTF-8，内部转 std::filesystem::path（Windows 下正确处理中文）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr);
bool writeFile(const std::string& path, const void* data, size_t size, std::string* error = nullptr);
bool appendFile(const std::string& path, const std::string& text, std::string* error = nullptr);

}  // namespace fx
