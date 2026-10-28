#include "settings/Json.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace clipback::json {

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

const Value* Value::find(const std::string& key) const
{
    if (!isObject()) return nullptr;
    for (const auto& kv : object_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

Value& Value::set(const std::string& key, Value v)
{
    if (!isObject()) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& kv : object_) {
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    }
    object_.emplace_back(key, std::move(v));
    return object_.back().second;
}

static void AppendEscaped(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

static void AppendIndent(std::string& out, int indent, int depth)
{
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(indent * depth), ' ');
}

void Value::serializeTo(std::string& out, int indent, int depth) const
{
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        if (std::isfinite(number_) && std::floor(number_) == number_ && std::fabs(number_) < 1e15) {
            out += std::to_string(static_cast<long long>(number_));
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", number_);
            out += buf;
        }
        break;
    }
    case Type::String: AppendEscaped(out, string_); break;
    case Type::Array: {
        out.push_back('[');
        for (size_t i = 0; i < array_.size(); ++i) {
            if (i) out.push_back(',');
            AppendIndent(out, indent, depth + 1);
            array_[i].serializeTo(out, indent, depth + 1);
        }
        if (!array_.empty()) AppendIndent(out, indent, depth);
        out.push_back(']');
        break;
    }
    case Type::Object: {
        out.push_back('{');
        for (size_t i = 0; i < object_.size(); ++i) {
            if (i) out.push_back(',');
            AppendIndent(out, indent, depth + 1);
            AppendEscaped(out, object_[i].first);
            out += indent > 0 ? ": " : ":";
            object_[i].second.serializeTo(out, indent, depth + 1);
        }
        if (!object_.empty()) AppendIndent(out, indent, depth);
        out.push_back('}');
        break;
    }
    }
}

std::string Value::serialize(int indent) const
{
    std::string out;
    serializeTo(out, indent, 0);
    if (indent > 0) out.push_back('\n');
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
    Parser(const std::string& text, std::string& error) : s_(text), err_(error) {}

    bool parse(Value& out)
    {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (pos_ != s_.size()) return fail("trailing characters");
        return true;
    }

private:
    bool fail(const char* msg)
    {
        err_ = std::string(msg) + " at offset " + std::to_string(pos_);
        return false;
    }

    void skipWs()
    {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            // Allow // and /* */ comments for hand-edited config files.
            if (c == '/' && pos_ + 1 < s_.size()) {
                if (s_[pos_ + 1] == '/') {
                    while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
                    continue;
                }
                if (s_[pos_ + 1] == '*') {
                    pos_ += 2;
                    while (pos_ + 1 < s_.size() && !(s_[pos_] == '*' && s_[pos_ + 1] == '/')) ++pos_;
                    pos_ = pos_ + 2 <= s_.size() ? pos_ + 2 : s_.size();
                    continue;
                }
            }
            break;
        }
    }

    bool parseValue(Value& out, int depth)
    {
        if (depth > 128) return fail("nesting too deep");
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') { std::string str; if (!parseString(str)) return false; out = Value(std::move(str)); return true; }
        if (c == 't') return parseLiteral("true", Value(true), out);
        if (c == 'f') return parseLiteral("false", Value(false), out);
        if (c == 'n') return parseLiteral("null", Value(), out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character");
    }

    bool parseLiteral(const char* lit, Value v, Value& out)
    {
        const size_t n = strlen(lit);
        if (s_.compare(pos_, n, lit) != 0) return fail("invalid literal");
        pos_ += n;
        out = std::move(v);
        return true;
    }

    bool parseNumber(Value& out)
    {
        const size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        const std::string tok = s_.substr(start, pos_ - start);
        char* end = nullptr;
        const double v = strtod(tok.c_str(), &end);
        if (!end || *end != '\0') return fail("invalid number");
        out = Value(v);
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }

    bool parseHex4(unsigned& v)
    {
        if (pos_ + 4 > s_.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad hex digit");
        }
        return true;
    }

    bool parseString(std::string& out)
    {
        ++pos_; // opening quote
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return fail("unterminated escape");
                const char e = s_[pos_++];
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
                    unsigned cp;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned lo;
                        if (!parseHex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parseArray(Value& out, int depth)
    {
        ++pos_;
        out = Value::Array();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        for (;;) {
            skipWs();
            Value item;
            if (!parseValue(item, depth + 1)) return false;
            out.push(std::move(item));
            skipWs();
            if (pos_ >= s_.size()) return fail("unterminated array");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parseObject(Value& out, int depth)
    {
        ++pos_;
        out = Value::Object();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        for (;;) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected key string");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.set(key, std::move(v));
            skipWs();
            if (pos_ >= s_.size()) return fail("unterminated object");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    const std::string& s_;
    std::string& err_;
    size_t pos_ = 0;
};

} // namespace

bool Parse(const std::string& text, Value& out, std::string& error)
{
    // Skip UTF-8 BOM
    std::string body = text;
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF && static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
        body.erase(0, 3);
    }
    Parser p(body, error);
    return p.parse(out);
}

} // namespace clipback::json
