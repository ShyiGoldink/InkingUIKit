#include <util/InkingJson.h>

#include <cstdlib>
#include <string>
#include <utility>

namespace inking::json {

namespace {

/**
 * 递归下降解析器。
 *
 * 手写而不是用正则 / 状态机生成器：JSON 的语法小到一屏能写完，而递归下降
 * 天然带着"当前在哪个字符"这个信息，行号与列号随手就有。
 */
class Parser {
public:
    explicit Parser(const std::string& text) : _text(text) {}

    Result Run() {
        Result result;
        SkipWhitespace();
        if (!ParseValue(result.value)) {
            result.ok = false;
            result.error = _error;
            return result;
        }
        SkipWhitespace();
        if (!AtEnd()) {
            Fail("文件末尾有多余内容（JSON 只允许一个顶层值）");
            result.ok = false;
            result.error = _error;
            return result;
        }
        result.ok = true;
        return result;
    }

private:
    const std::string& _text;
    std::size_t _pos = 0;
    int _line = 1;
    int _column = 1;
    std::string _error;

    bool AtEnd() const { return _pos >= _text.size(); }
    char Peek() const { return AtEnd() ? '\0' : _text[_pos]; }

    SourceLocation Here() const { return SourceLocation{_line, _column}; }

    char Advance() {
        if (AtEnd()) {
            return '\0';
        }
        const char ch = _text[_pos++];
        if (ch == '\n') {
            ++_line;
            _column = 1;
        } else {
            ++_column;
        }
        return ch;
    }

    void SkipWhitespace() {
        while (!AtEnd()) {
            const char ch = Peek();
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                Advance();
                continue;
            }
            break;
        }
    }

    /// 记录一条错误，并带上当前位置。只记第一条——后面往往是它的连锁反应。
    bool Fail(const std::string& message) {
        if (_error.empty()) {
            _error = "第 " + std::to_string(_line) + " 行第 "
                   + std::to_string(_column) + " 列：" + message;
        }
        return false;
    }

    bool ParseValue(Value& out) {
        SkipWhitespace();
        if (AtEnd()) {
            return Fail("内容不完整：这里应该有一个值");
        }

        out.where = Here();
        switch (Peek()) {
            case '{':
                return ParseObject(out);
            case '[':
                return ParseArray(out);
            case '"':
                out.kind = Value::Kind::String;
                return ParseString(out.text);
            case 't':
                return ParseKeyword("true", out, true);
            case 'f':
                return ParseKeyword("false", out, false);
            case 'n': {
                if (!ParseKeyword("null", out, false)) {
                    return false;
                }
                out.kind = Value::Kind::Null;
                return true;
            }
            default:
                return ParseNumber(out);
        }
    }

    bool ParseKeyword(const char* word, Value& out, bool value) {
        for (const char* p = word; *p != '\0'; ++p) {
            if (Peek() != *p) {
                return Fail(std::string("不认识的写法，是想写 \"") + word + "\" 吗");
            }
            Advance();
        }
        out.kind = Value::Kind::Bool;
        out.boolean = value;
        return true;
    }

    bool ParseObject(Value& out) {
        out.kind = Value::Kind::Object;
        Advance();  // '{'
        SkipWhitespace();

        if (Peek() == '}') {
            Advance();
            return true;
        }

        while (true) {
            SkipWhitespace();
            if (Peek() != '"') {
                return Fail("对象的键必须是字符串（要写双引号）");
            }

            const SourceLocation keyWhere = Here();
            std::string key;
            if (!ParseString(key)) {
                return false;
            }

            // 重复键当场报错：后一个静默胜出是最难查的一类配置错误。
            for (const auto& field : out.fields) {
                if (field.first == key) {
                    return Fail("字段 \"" + key + "\" 重复定义（第 "
                                + std::to_string(field.second.where.line)
                                + " 行已经写过一次）");
                }
            }

            SkipWhitespace();
            if (Peek() != ':') {
                return Fail("字段 \"" + key + "\" 后面要跟一个冒号");
            }
            Advance();

            Value child;
            if (!ParseValue(child)) {
                return false;
            }
            // 值的行号用键的行号：报"字段 xxx 有问题"时指到字段本身更好找。
            child.where = keyWhere;
            out.fields.emplace_back(key, std::move(child));

            SkipWhitespace();
            if (Peek() == ',') {
                Advance();
                continue;
            }
            if (Peek() == '}') {
                Advance();
                return true;
            }
            if (AtEnd()) {
                return Fail("对象没有闭合（少了 '}'）");
            }
            return Fail("对象里只能是 '字段: 值' 用逗号分隔（尾随逗号也不允许）");
        }
    }

    bool ParseArray(Value& out) {
        out.kind = Value::Kind::Array;
        Advance();  // '['
        SkipWhitespace();

        if (Peek() == ']') {
            Advance();
            return true;
        }

        while (true) {
            Value child;
            if (!ParseValue(child)) {
                return false;
            }
            out.items.push_back(std::move(child));

            SkipWhitespace();
            if (Peek() == ',') {
                Advance();
                continue;
            }
            if (Peek() == ']') {
                Advance();
                return true;
            }
            if (AtEnd()) {
                return Fail("数组没有闭合（少了 ']'）");
            }
            return Fail("数组里只能是值用逗号分隔（尾随逗号也不允许）");
        }
    }

    bool ParseString(std::string& out) {
        Advance();  // '"'
        out.clear();

        while (true) {
            if (AtEnd()) {
                return Fail("字符串没有闭合（少了 '\"'）");
            }
            const char ch = Advance();
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                // 配置是 UTF-8 的，中文原样收进来（不转义）。
                out.push_back(ch);
                continue;
            }

            if (AtEnd()) {
                return Fail("转义符后面没有内容");
            }
            const char escape = Advance();
            switch (escape) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    // 只处理基本多文种平面：把 \uXXXX 编成 UTF-8。
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (AtEnd()) {
                            return Fail("\\u 转义后面要跟 4 位十六进制");
                        }
                        const char digit = Advance();
                        code <<= 4;
                        if (digit >= '0' && digit <= '9') {
                            code |= static_cast<unsigned>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            code |= static_cast<unsigned>(digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            code |= static_cast<unsigned>(digit - 'A' + 10);
                        } else {
                            return Fail("\\u 转义里出现了非十六进制字符");
                        }
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    return Fail(std::string("不认识的转义：\\") + escape);
            }
        }
    }

    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80u) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = _pos;
        if (Peek() == '-') {
            Advance();
        }
        bool sawDigit = false;
        while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
            Advance();
            sawDigit = true;
        }
        if (Peek() == '.') {
            Advance();
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
                Advance();
                sawDigit = true;
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            Advance();
            if (Peek() == '+' || Peek() == '-') {
                Advance();
            }
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
                Advance();
                sawDigit = true;
            }
        }

        if (!sawDigit) {
            return Fail(std::string("这里应该是一个值，但看到了 '") + Peek() + "'");
        }

        const std::string token = _text.substr(start, _pos - start);
        out.kind = Value::Kind::Number;
        out.number = std::strtod(token.c_str(), nullptr);
        return true;
    }
};

}  // namespace

const Value* Value::Find(const std::string& key) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (const auto& field : fields) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

const char* Value::KindName() const {
    switch (kind) {
        case Kind::Null:   return "null";
        case Kind::Bool:   return "布尔";
        case Kind::Number: return "数字";
        case Kind::String: return "字符串";
        case Kind::Array:  return "数组";
        case Kind::Object: return "对象";
    }
    return "未知";
}

Result Parse(const std::string& text) {
    // UTF-8 BOM 在这里统一去掉：记事本"另存为 UTF-8"会带上它，而 JSON 规范
    // 不允许在值前面有别的东西。放进 Parse 而不是 LoadJsonFile，
    // 是为了让所有入口（文件、自检里的字面量）行为一致。
    const std::string body =
        text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEFu
                && static_cast<unsigned char>(text[1]) == 0xBBu
                && static_cast<unsigned char>(text[2]) == 0xBFu
            ? text.substr(3)
            : text;

    Parser parser(body);
    return parser.Run();
}

const Value* Value::At(std::size_t index) const {
    return kind == Kind::Array && index < items.size() ? &items[index] : nullptr;
}

std::string ReadString(const Value& object, const std::string& key,
                       const std::string& fallback) {
    const Value* field = object.Find(key);
    return field != nullptr && field->IsString() ? field->text : fallback;
}

double ReadNumber(const Value& object, const std::string& key, double fallback) {
    const Value* field = object.Find(key);
    return field != nullptr && field->IsNumber() ? field->number : fallback;
}

bool ReadBool(const Value& object, const std::string& key, bool fallback) {
    const Value* field = object.Find(key);
    return field != nullptr && field->IsBool() ? field->boolean : fallback;
}

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (raw) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20u) {
                    // 其余控制字符走 \u00XX：JSON 不允许裸的控制字节。
                    static const char* const kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(byte >> 4) & 0x0Fu];
                    out += kHex[byte & 0x0Fu];
                } else {
                    out += raw;  // 中文（UTF-8 多字节）原样出去
                }
                break;
        }
    }
    return out;
}

}  // namespace inking::json
