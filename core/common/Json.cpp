#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Common.h"

namespace orsend {

namespace {
const Json kNullJson;

void AppendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void AppendNumber(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "0";
        return;
    }
    // 整数值按整数输出，避免出现 1.0 这种不友好的形式
    if (v == static_cast<double>(static_cast<int64_t>(v)) && std::fabs(v) < 9.0e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        out += buf;
    } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        out += buf;
    }
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    Parser(const std::string& text, std::string* error) : s_(text), error_(error) {}

    bool Run(Json& out) {
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        if (pos_ != s_.size()) return Fail("trailing characters");
        return true;
    }

private:
    bool Fail(const char* msg) {
        if (error_) {
            *error_ = std::string(msg) + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    void SkipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool ParseValue(Json& out) {
        if (pos_ >= s_.size()) return Fail("unexpected end");
        char c = s_[pos_];
        switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string str;
                if (!ParseString(str)) return false;
                out = Json(std::move(str));
                return true;
            }
            case 't':
                if (s_.compare(pos_, 4, "true") == 0) {
                    pos_ += 4;
                    out = Json(true);
                    return true;
                }
                return Fail("invalid token");
            case 'f':
                if (s_.compare(pos_, 5, "false") == 0) {
                    pos_ += 5;
                    out = Json(false);
                    return true;
                }
                return Fail("invalid token");
            case 'n':
                if (s_.compare(pos_, 4, "null") == 0) {
                    pos_ += 4;
                    out = Json();
                    return true;
                }
                return Fail("invalid token");
            default: return ParseNumber(out);
        }
    }

    bool ParseNumber(Json& out) {
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                    s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+')) {
            ++pos_;
        }
        if (pos_ == start) return Fail("invalid value");
        std::string numStr = s_.substr(start, pos_ - start);
        char* end = nullptr;
        double v = std::strtod(numStr.c_str(), &end);
        if (end == numStr.c_str()) return Fail("invalid number");
        out = Json(v);
        return true;
    }

    static int HexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ParseHex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return Fail("invalid unicode escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = HexVal(s_[pos_ + i]);
            if (h < 0) return Fail("invalid unicode escape");
            v = (v << 4) | static_cast<uint32_t>(h);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool ParseString(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return Fail("expected string");
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) return Fail("invalid escape");
            char e = s_[pos_++];
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
                    uint32_t cp = 0;
                    if (!ParseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t lo = 0;
                        if (!ParseHex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return Fail("invalid escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseArray(Json& out) {
        ++pos_;  // '['
        out = Json::MakeArray();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWs();
            Json item;
            if (!ParseValue(item)) return false;
            out.Push(std::move(item));
            SkipWs();
            if (pos_ >= s_.size()) return Fail("unterminated array");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return Fail("expected ',' or ']'");
        }
    }

    bool ParseObject(Json& out) {
        ++pos_;  // '{'
        out = Json::MakeObject();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return Fail("expected ':'");
            ++pos_;
            SkipWs();
            Json value;
            if (!ParseValue(value)) return false;
            out[key] = std::move(value);
            SkipWs();
            if (pos_ >= s_.size()) return Fail("unterminated object");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return Fail("expected ',' or '}'");
        }
    }

    const std::string& s_;
    std::string* error_;
    size_t pos_ = 0;
};

}  // namespace

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object) {
        for (const auto& kv : obj_) {
            if (kv.first == key) return kv.second;
        }
    }
    return kNullJson;
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
    }
    for (auto& kv : obj_) {
        if (kv.first == key) return kv.second;
    }
    obj_.emplace_back(key, Json());
    return obj_.back().second;
}

const Json& Json::operator[](size_t index) const {
    if (type_ == Type::Array && index < arr_.size()) return arr_[index];
    return kNullJson;
}

Json& Json::operator[](size_t index) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    if (index >= arr_.size()) arr_.resize(index + 1);
    return arr_[index];
}

bool Json::Has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_) {
        if (kv.first == key) return true;
    }
    return false;
}

size_t Json::Size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

void Json::Push(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    arr_.push_back(std::move(value));
}

void Json::Erase(const std::string& key) {
    if (type_ != Type::Object) return;
    for (auto it = obj_.begin(); it != obj_.end(); ++it) {
        if (it->first == key) {
            obj_.erase(it);
            return;
        }
    }
}

std::string Json::AsString(const std::string& def) const {
    if (type_ == Type::String) return str_;
    if (type_ == Type::Number) {
        std::string tmp;
        AppendNumber(tmp, num_);
        return tmp;
    }
    if (type_ == Type::Bool) return bool_ ? "true" : "false";
    return def;
}

int Json::AsInt(int def) const { return static_cast<int>(AsInt64(def)); }

int64_t Json::AsInt64(int64_t def) const {
    if (type_ == Type::Number) return static_cast<int64_t>(num_);
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    if (type_ == Type::String) {
        char* end = nullptr;
        long long v = std::strtoll(str_.c_str(), &end, 10);
        if (end && *end == '\0' && end != str_.c_str()) return v;
    }
    return def;
}

uint64_t Json::AsUInt64(uint64_t def) const {
    int64_t v = AsInt64(static_cast<int64_t>(def));
    return v < 0 ? def : static_cast<uint64_t>(v);
}

double Json::AsDouble(double def) const {
    if (type_ == Type::Number) return num_;
    if (type_ == Type::String) {
        char* end = nullptr;
        double v = std::strtod(str_.c_str(), &end);
        if (end && *end == '\0' && end != str_.c_str()) return v;
    }
    return def;
}

bool Json::AsBool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return num_ != 0.0;
    if (type_ == Type::String) {
        if (IEquals(str_, "true") || str_ == "1") return true;
        if (IEquals(str_, "false") || str_ == "0") return false;
    }
    return def;
}

std::string Json::Dump(int indent) const {
    std::string out;
    out.reserve(256);
    DumpTo(out, indent, 0);
    return out;
}

void Json::DumpTo(std::string& out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    auto newline = [&](int d) {
        if (!pretty) return;
        out.push_back('\n');
        out.append(static_cast<size_t>(indent * d), ' ');
    };

    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: AppendNumber(out, num_); break;
        case Type::String: AppendEscaped(out, str_); break;
        case Type::Array: {
            if (arr_.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out.push_back(',');
                newline(depth + 1);
                arr_[i].DumpTo(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (obj_.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out.push_back(',');
                newline(depth + 1);
                AppendEscaped(out, obj_[i].first);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                obj_[i].second.DumpTo(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
        }
    }
}

bool Json::Parse(const std::string& text, Json& out, std::string* error) {
    Parser p(text, error);
    return p.Run(out);
}

}  // namespace orsend
