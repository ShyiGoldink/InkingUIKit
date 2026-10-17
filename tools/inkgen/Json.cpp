#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace inkgen {

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

    ParseResult Run() {
        ParseResult result;
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

    bool ParseValue(JsonValue& out) {
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
                out.kind = JsonValue::Kind::String;
                return ParseString(out.text);
            case 't':
                return ParseKeyword("true", out, true);
            case 'f':
                return ParseKeyword("false", out, false);
            case 'n': {
                if (!ParseKeyword("null", out, false)) {
                    return false;
                }
                out.kind = JsonValue::Kind::Null;
                return true;
            }
            default:
                return ParseNumber(out);
        }
    }

    bool ParseKeyword(const char* word, JsonValue& out, bool value) {
        for (const char* p = word; *p != '\0'; ++p) {
            if (Peek() != *p) {
                return Fail(std::string("不认识的写法，是想写 \"") + word + "\" 吗");
            }
            Advance();
        }
        out.kind = JsonValue::Kind::Bool;
        out.boolean = value;
        return true;
    }

    bool ParseObject(JsonValue& out) {
        out.kind = JsonValue::Kind::Object;
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

            JsonValue child;
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

    bool ParseArray(JsonValue& out) {
        out.kind = JsonValue::Kind::Array;
        Advance();  // '['
        SkipWhitespace();

        if (Peek() == ']') {
            Advance();
            return true;
        }

        while (true) {
            JsonValue child;
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

    bool ParseNumber(JsonValue& out) {
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
        out.kind = JsonValue::Kind::Number;
        out.number = std::strtod(token.c_str(), nullptr);
        return true;
    }
};

}  // namespace

const JsonValue* JsonValue::Find(const std::string& key) const {
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

const char* JsonValue::KindName() const {
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

ParseResult ParseJson(const std::string& text) {
    // UTF-8 BOM 在这里统一去掉：记事本"另存为 UTF-8"会带上它，而 JSON 规范
    // 不允许在值前面有别的东西。放进 ParseJson 而不是 LoadJsonFile，
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

ParseResult LoadJsonFile(const std::string& path) {
    ParseResult result;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.ok = false;
        result.error = "打不开文件：" + path;
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();

    // BOM 由 ParseJson 统一处理，这里不重复。
    result = ParseJson(buffer.str());
    if (!result.ok) {
        // ParseJson 的错误已经带"第 N 行第 M 列"，这里只补文件名前缀——
        // 两边都补一次会得到 "路径：路径：第 N 行…"（踩过）。
        result.error = path + "：" + result.error;
    }
    return result;
}

}  // namespace inkgen
