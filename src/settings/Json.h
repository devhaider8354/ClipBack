// Minimal self-contained JSON value / parser / serializer (UTF-8).
// Written in-house to avoid an external dependency for a ~15 key config file.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace clipback::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Value(unsigned v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Value(int64_t v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Value(double v) : type_(Type::Number), number_(v) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Value Array() { Value v; v.type_ = Type::Array; return v; }
    static Value Object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
    double asNumber(double def = 0.0) const { return isNumber() ? number_ : def; }
    int asInt(int def = 0) const { return isNumber() ? static_cast<int>(number_) : def; }
    const std::string& asString() const { return string_; }
    std::string asString(const std::string& def) const { return isString() ? string_ : def; }

    // Object access
    const Value* find(const std::string& key) const;
    Value& set(const std::string& key, Value v);
    bool has(const std::string& key) const { return find(key) != nullptr; }
    const std::vector<std::pair<std::string, Value>>& members() const { return object_; }

    // Array access
    void push(Value v) { array_.push_back(std::move(v)); }
    const std::vector<Value>& items() const { return array_; }

    std::string serialize(int indent = 2) const;

private:
    void serializeTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

// Returns false and fills `error` on malformed input.
bool Parse(const std::string& text, Value& out, std::string& error);

} // namespace clipback::json
