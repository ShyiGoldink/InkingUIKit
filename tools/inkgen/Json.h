#pragma once

// inkgen 自带的极简 JSON 解析器。
//
// 为什么不引第三方：CXXCSS 是**我们自己的格式**，用的只是 JSON 的一个很小子集
// （对象 / 数组 / 字符串 / 数字 / 布尔 / null + $comment），而且我们需要的两样
// 东西第三方库不一定给：
//   1. **每个值的行号**——"radius 必须为正"这种报错要指到行，否则用户在几百行
//      配置里只能自己找；
//   2. **重复键报错**——`{"type":"color","type":"img"}` 在大多数解析器里后一个
//      静默胜出，而 CXXCSS.md §2.1 定的调子是"拼错的字段比缺字段更难查"。
//
// 生成器是构建期工具，不进二进制、不参与运行期性能，所以这里优先"读得懂 + 报错准"，
// 不追求解析速度。

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace inkgen {

/// 一个 JSON 值在源文件里的位置。报错全靠它。
struct SourceLocation {
    /// 1 起；0 表示"没有来源"（例如父节点等子节点时构造的中间对象）。
    int line = 0;
    int column = 0;
};

/**
 * @brief 解析出来的 JSON 值。
 *
 * 对象用**有序 vector** 存键值对，不用 map：生成器要按配置里写的顺序输出，
 * 而且键的插入顺序也是查重报错时最先给出的信息。
 */
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    SourceLocation where{};

    bool boolean = false;
    double number = 0.0;
    std::string text;

    std::vector<JsonValue> items;                          ///< Array
    std::vector<std::pair<std::string, JsonValue>> fields; ///< Object（保序）

    bool IsNull() const { return kind == Kind::Null; }
    bool IsBool() const { return kind == Kind::Bool; }
    bool IsNumber() const { return kind == Kind::Number; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray() const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }

    /// 对象里找字段；没有返回 nullptr。
    const JsonValue* Find(const std::string& key) const;

    /// 这个值长什么样（"字符串" / "数组" …），报错信息里用。
    const char* KindName() const;
};

/// 解析结果：要么有值，要么有一份"人能看懂"的错误。
struct ParseResult {
    /// 解析成功时为 true；这时 `value` 有效。
    bool ok = false;
    JsonValue value;

    /// 失败原因（含行号），例如 `第 7 行第 5 列：字段 "radius" 重复定义`。
    std::string error;
};

/**
 * @brief 解析一段 JSON 文本。
 *
 * 严格模式：不允许尾随逗号、不允许单引号字符串、不允许 `//` 注释
 * （注释请用 `$comment` 字段，CXXCSS.md §2.1 就是这么定的）。
 */
ParseResult ParseJson(const std::string& text);

/// 读文件 + ParseJson。读不开时返回 ok=false，错误里带文件名。
ParseResult LoadJsonFile(const std::string& path);

}  // namespace inkgen
