// 轻量 JSON 库：不引入第三方依赖，供配置解析与控制接口使用。
#pragma once
#ifndef ORSEND_JSON_H
#define ORSEND_JSON_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace orsend {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using ArrayType = std::vector<Json>;
    using ObjectType = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(unsigned v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(unsigned long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(unsigned long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(float v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
    Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

    static Json MakeArray() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json MakeObject() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    // 对象访问：不存在时返回一个共享的 null 节点（只读）
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);

    // 数组访问
    const Json& operator[](size_t index) const;
    Json& operator[](size_t index);

    bool Has(const std::string& key) const;
    size_t Size() const;

    void Push(Json value);
    void Erase(const std::string& key);

    const ObjectType& Items() const { return obj_; }
    const ArrayType& Elements() const { return arr_; }

    // 取值（带默认值，类型不符时返回默认值）
    std::string AsString(const std::string& def = std::string()) const;
    int AsInt(int def = 0) const;
    int64_t AsInt64(int64_t def = 0) const;
    uint64_t AsUInt64(uint64_t def = 0) const;
    double AsDouble(double def = 0.0) const;
    bool AsBool(bool def = false) const;

    // 序列化：indent < 0 表示紧凑输出
    std::string Dump(int indent = -1) const;

    static bool Parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void DumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    ArrayType arr_;
    ObjectType obj_;
};

}  // namespace orsend

#endif  // ORSEND_JSON_H
