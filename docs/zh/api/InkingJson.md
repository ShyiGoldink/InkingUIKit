# `inking::json`：库内的 JSON 小工具（0.6.5）

> 头文件 `<util/InkingJson.h>` ｜ 实现 `src/util/InkingJson.cpp`（+ `ParseAsset` 在
> `src/asset/InkingAsset.cpp`）｜ 只依赖标准库

它原来只是生成器的私有件（`tools/inkgen/Json.{h,cpp}`，495 行），0.6.5 提升成库的一部分。
留下来而不是引第三方的三条理由，也是这套实现的三条性质：

1. **每个值带行号**——"`radius` 必须为正"这种报错要指到行，否则几百行配置里等于没报；
2. **重复键报错**——大多数库让后一个静默胜出，而"拼错的字段比缺字段更难查"；
3. **零异常**——坏输入走 `Result::error`。生成期与运行期都不想让 `bad_alloc` 之外的
   异常穿过界面回调。

## 一、类型

```C++
struct SourceLocation { int line = 0; int column = 0; };   // 1 起；0 = 没有来源

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    SourceLocation where;          // 这个值在源文本里的位置（报错全靠它）
    std::string file;              // 跨文件合并时填：值来自哪个文件
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;                          // Array
    std::vector<std::pair<std::string, Value>> fields; // Object（**保序**）

    bool IsNull() const noexcept;   // IsBool / IsNumber / IsString / IsArray / IsObject 同形
    const Value* Find(const std::string& key) const;   // 对象取字段，没有给 nullptr
    const Value* At(std::size_t index) const;          // 数组取项，越界给 nullptr
    const char* KindName() const;                      // "字符串" / "数组" …（报错文案用）
};

struct Result { bool ok = false; Value value; std::string error; };
```

`fields` 是**保序的 `vector`** 而不是 `map`：生成器要"按配置里写的顺序输出"，
"读进来再写出去"也保持顺序。代价是查字段线性扫——配置表都很小，可接受。
**这条不可回头**：要给"快速按键查找"只能在 `Find` 内部加索引，别改结构。

## 二、函数

```C++
Result Parse(const std::string& text);              // 纯字符串，不碰文件系统
Result ParseAsset(const std::string& relative);     // AssetLoadText + Parse（相对资源根）

std::string ReadString(const Value& object, const std::string& key,
                       const std::string& fallback);
double ReadNumber(const Value& object, const std::string& key, double fallback);
bool   ReadBool(const Value& object, const std::string& key, bool fallback);

std::string Escape(const std::string& text);        // 写 json 时转义
```

`Parse` 是**严格模式**：不允许尾随逗号、单引号字符串、`//` 注释（注释用 `$comment`
字段）。开头的 UTF-8 BOM 会被去掉（记事本"另存为 UTF-8"会带上它）。

三个 `ReadXxx` 是"小工具"与"库里那套契约"的分界线：**取不到或类型不对就给 fallback**。
必填字段请继续用 `Find` + 自己报错——那样才能带上 `where`（行号）。注意 `false`
与"缺字段"是分得开的：`ReadBool(v, "loop", true)` 在 `loop: false` 上返回 `false`。

## 三、两条路径语义，别混

| 入口 | 路径基准 | 谁在用 |
| --- | --- | --- |
| `ParseAsset(relative)` | **资源根**（默认 exe 旁的 `assets/`，认混淆头） | 运行期 |
| `LoadJsonFile(path)`（**工具专有**，不在库里） | **当前目录** | `inkgen` 读命令行给的路径 |

`ParseAsset` 的实现放在 `src/asset/InkingAsset.cpp` 而不是 `src/util/InkingJson.cpp`：
后者**也被生成器编一份**（`cmake/inkgen.cmake`，工具不链接 DLL），而生成器不该为了一个
它不调的包装函数去拖资产层（那里有 SDL 的 IO）。

## 四、错误文案长什么样

```
第 3 行第 6 列：字段 "a" 重复定义（第 2 行已经写过一次）
第 3 行第 1 列：对象没有闭合（少了 '}'）
第 3 行第 1 列：对象的键必须是字符串（要写双引号）
```

`ParseAsset` / `LoadJsonFile` 会在前面补来源前缀（`atlas.json：第 7 行…`）——
**只补一次**：两边都补会得到"路径：路径：第 N 行…"。

## 五、不用它的场合

- 想**热更**配置（改文件不重编）：本项目不做，见任务书 §11（结构进 exe、字节进 assets）；
- 需要 `\uXXXX` 的纯 ASCII 输出：`Escape` 刻意保留中文原样，要转义请自己再过一遍；
- 只需要读一个数 / 一个字符串：为这一个值引这套类型不划算，直接读文件自己解析更快。

---

> 本文档对应版本：**0.6.6（0.x 发布版）**。
