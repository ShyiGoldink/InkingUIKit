#pragma once

// 一个**够用就好**的 JSON 小工具（0.6.5 T-15）：解析 + 取值 + 转义。
//
// 它原来只住在 `tools/inkgen/`（生成器的私有件），现在提升成库的一部分，理由三条：
//   - "读一份配置"这件事不该逼使用者去引第三方（本项目自己的 495 行就够，且是
//     **零异常 + 带行列号 + 重复键报错**，正是配置类项目真正需要的三样）；
//   - 生成器与库**共用同一份实现**（否则"生成期读得懂、运行期读不懂"是迟早的事，
//     而那种分叉最难查：同一份文件两边报错不同）；
//   - 它只依赖标准库，不进任何 TU 就是零代价（不 include 就不参与编译）。
//
// ---------------------------------------------------------------------------
// 三条设计约定（都是不可回头的选择，改之前先想清楚）
//
//   1. **`fields` 是保序的 `vector`，不是 `map`**：生成器要"按配置里写的顺序输出"，
//      "读进来再写出去"也要保持顺序。代价是查字段线性扫——配置表都很小，可接受。
//      要更快只能在 `Find` 内部优化（例如给对象加一个懒建的索引），**别改结构**。
//   2. **零异常**：坏输入永远走 `Result::error`，不抛。语法错是"必须报"的，
//      取不到字段是"能容错"的——后者由 `ReadXxx` 给 fallback 兜住。
//   3. **解析器只认字符串**：文件读取分两个入口——`ParseAsset` 走资产层
//      （已处理资源根 / 宽路径 / 混淆头），工具的 `LoadJsonFile` 走当前目录。
//      两套路径语义不同，塞进解析器就会有一边是错的。
//
// 报错文案形如 `第 7 行第 5 列：字段 "radius" 重复定义`——行号是这套实现存在的
// 主要理由（几百行配置里没有行号等于没有报错）。

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace inking::json {

/// 一个值在源文本里的位置。1 起；`0` 表示"没有来源"（解析器自己造的中间对象）。
struct SourceLocation {
    int line = 0;
    int column = 0;
};

/**
 * @brief 解析出来的 JSON 值。
 *
 * 一个类型装六种形态（`Kind` + 各自那份载荷），而不是 `variant` / 继承：这套实现的
 * 消费者（配置读取）是"先问类型再取字段"，扁平结构读起来最短，也省掉一堆 `std::get`。
 */
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    SourceLocation where{};

    /**
     * 这个值**来自哪个文件**（跨文件合并时才填）。
     *
     * 空 = 就是当前正在读的那份。`$extends` 那种"父文件的值搬进子文件"的场景里，
     * `where.line` 指向父文件的行——报错必须同时指对文件，否则用户会去子文件的
     * 同一行找（那里什么都没有）。
     */
    std::string file;

    bool boolean = false;
    double number = 0.0;
    std::string text;

    std::vector<Value> items;                           ///< Array
    std::vector<std::pair<std::string, Value>> fields;  ///< Object（**保序**）

    bool IsNull() const noexcept { return kind == Kind::Null; }
    bool IsBool() const noexcept { return kind == Kind::Bool; }
    bool IsNumber() const noexcept { return kind == Kind::Number; }
    bool IsString() const noexcept { return kind == Kind::String; }
    bool IsArray() const noexcept { return kind == Kind::Array; }
    bool IsObject() const noexcept { return kind == Kind::Object; }

    /// 对象里找字段；没有（或自己不是对象）返回 nullptr。
    const Value* Find(const std::string& key) const;

    /// 数组里取第 `index` 项；越界（或自己不是数组）返回 nullptr。
    const Value* At(std::size_t index) const;

    /// 这个值长什么样（"字符串" / "数组" …），报错文案里用。
    const char* KindName() const;
};

/// 解析结果：要么有值，要么有一份"人能看懂"的错误。
struct Result {
    bool ok = false;
    Value value;

    /// 失败原因（含行列号），例如 `第 7 行第 5 列：字段 "radius" 重复定义`。
    std::string error;
};

/**
 * @brief 解析一段 JSON 文本。**纯字符串**，不碰文件系统。
 *
 * 严格模式：不允许尾随逗号、单引号字符串、`//` 注释（注释用 `$comment` 字段）。
 * 开头的 UTF-8 BOM 会被去掉（记事本"另存为 UTF-8"会带上它）。
 */
Result Parse(const std::string& text);

/**
 * @brief 从**资源根**读一个相对路径并解析（`AssetLoadText` + `Parse`）。
 *
 * 与工具里的 `LoadJsonFile` 的区别就是路径基准：这里相对资源根（exe 旁的
 * `assets/`），那里相对当前目录。实现放在资产层（`src/asset/InkingAsset.cpp`）——
 * 本文件那份实现也被生成器编一份，而生成器不链接资产层。
 *
 * @return 读不到 / 解析失败都返回 `ok == false`，`error` 里带路径。
 */
Result ParseAsset(const std::string& relative);

// ---------------------------------------------------------------------------
// 便捷取值：取不到或类型不对就给 fallback
// ---------------------------------------------------------------------------
//
// 这三个函数是"小工具"与"库里的契约"那条分界线：配置文件里少一个字段通常该有
// 默认值，而不是让调用方写一串 `if (const Value* f = ...)`。要求严格的场合
// （必填字段）继续用 `Find` + 自己报错——那样才能带上`where`。

/// 取字符串；不存在 / 不是字符串时给 `fallback`。
std::string ReadString(const Value& object, const std::string& key,
                       const std::string& fallback);

/// 取数字；不存在 / 不是数字时给 `fallback`。
double ReadNumber(const Value& object, const std::string& key, double fallback);

/// 取布尔；不存在 / 不是布尔时给 `fallback`。
bool ReadBool(const Value& object, const std::string& key, bool fallback);

/**
 * @brief 写 JSON 时的字符串转义（引号 / 反斜杠 / 控制字符；**中文原样**）。
 *
 * 不转义 `\u`：输出保持 UTF-8 可读，谁要把非 ASCII 变成 `\uXXXX` 请自己再转一遍
 * （那是"给纯 ASCII 传输用"的另一种需求，别混在这里）。
 * 往返性质由自检保证：`Parse("{\"s\":\"" + Escape(x) + "\"}")` 取回的 `x` 逐字节相等。
 */
std::string Escape(const std::string& text);

}  // namespace inking::json
