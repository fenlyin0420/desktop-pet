// 极简 JSON 解析/序列化：够读 config.json 与 model3.json 即可。
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : type_(Type::Bool), num_(b ? 1.0 : 0.0) {}
    Value(double d) : type_(Type::Number), num_(d) {}
    Value(int i) : type_(Type::Number), num_(i) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    double num(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    int integer(int def = 0) const { return type_ == Type::Number ? static_cast<int>(num_ < 0 ? num_ - 0.5 : num_ + 0.5) : def; }
    bool boolean(bool def = false) const {
        if (type_ == Type::Bool) return num_ != 0.0;
        if (type_ == Type::Number) return num_ != 0.0;
        return def;
    }
    const std::string& str() const { return str_; }
    const Array& arr() const { return arr_; }
    const Object& obj() const { return obj_; }
    Array& arr() { return arr_; }
    Object& obj() { return obj_; }

    const Value& operator[](const std::string& key) const {
        static const Value empty;
        if (type_ != Type::Object) return empty;
        auto it = obj_.find(key);
        return it == obj_.end() ? empty : it->second;
    }
    const Value& at(size_t idx) const {
        static const Value empty;
        if (type_ != Type::Array || idx >= arr_.size()) return empty;
        return arr_[idx];
    }
    size_t size() const {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        return 0;
    }
    bool has(const std::string& key) const { return type_ == Type::Object && obj_.count(key) != 0; }

private:
    friend class Parser;
    Type type_ = Type::Null;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;
};

class Parser {
public:
    static bool parse(const std::string& text, Value& out) {
        Parser p(text);
        p.skipWs();
        if (!p.parseValue(out)) return false;
        return true;
    }

private:
    explicit Parser(const std::string& s) : s_(s) {}

    const std::string& s_;
    size_t i_ = 0;

    bool eof() const { return i_ >= s_.size(); }
    char peek() const { return eof() ? '\0' : s_[i_]; }
    void skipWs() {
        while (!eof()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\xEF' /*BOM*/) {
                ++i_;
            } else {
                break;
            }
        }
        if (i_ == 0 && s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            i_ = 3;
        }
    }

    bool parseValue(Value& out) {
        skipWs();
        if (eof()) return false;
        char c = peek();
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Value(std::move(s));
            return true;
        }
        if (c == 't' && s_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            out = Value(true);
            return true;
        }
        if (c == 'f' && s_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            out = Value(false);
            return true;
        }
        if (c == 'n' && s_.compare(i_, 4, "null") == 0) {
            i_ += 4;
            out = Value();
            return true;
        }
        return parseNumber(out);
    }

    bool parseObject(Value& out) {
        out = Value();
        out.type_ = Value::Type::Object;
        ++i_;  // {
        skipWs();
        if (peek() == '}') {
            ++i_;
            return true;
        }
        while (!eof()) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (peek() != ':') return false;
            ++i_;
            Value v;
            if (!parseValue(v)) return false;
            out.obj_[key] = std::move(v);
            skipWs();
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == '}') {
                ++i_;
                return true;
            }
            return false;
        }
        return false;
    }

    bool parseArray(Value& out) {
        out = Value();
        out.type_ = Value::Type::Array;
        ++i_;  // [
        skipWs();
        if (peek() == ']') {
            ++i_;
            return true;
        }
        while (!eof()) {
            Value v;
            if (!parseValue(v)) return false;
            out.arr_.push_back(std::move(v));
            skipWs();
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == ']') {
                ++i_;
                return true;
            }
            return false;
        }
        return false;
    }

    bool parseString(std::string& out) {
        out.clear();
        skipWs();
        if (peek() != '"') return false;
        ++i_;
        while (!eof()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (eof()) return false;
            char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    for (int k = 0; k < 4 && !eof(); ++k) {
                        char h = s_[i_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                    }
                    // UTF-8 编码（不处理代理对：配置文件里只有普通汉字）
                    if (cp < 0x80) {
                        out.push_back(char(cp));
                    } else if (cp < 0x800) {
                        out.push_back(char(0xC0 | (cp >> 6)));
                        out.push_back(char(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(char(0xE0 | (cp >> 12)));
                        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(char(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(e); break;
            }
        }
        return false;
    }

    bool parseNumber(Value& out) {
        size_t start = i_;
        if (peek() == '-' || peek() == '+') ++i_;
        while (!eof() && (isdigit(static_cast<unsigned char>(peek())) || peek() == '.' || peek() == 'e' || peek() == 'E' || peek() == '-' ||
                          peek() == '+')) {
            ++i_;
        }
        if (start == i_) return false;
        out = Value(std::strtod(s_.substr(start, i_ - start).c_str(), nullptr));
        return true;
    }
};

inline bool loadFile(const std::string& path, Value& out, std::string* error = nullptr) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "无法打开 " + path;
        return false;
    }
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    if (!Parser::parse(text, out)) {
        if (error) *error = "JSON 解析失败: " + path;
        return false;
    }
    return true;
}

}  // namespace json
