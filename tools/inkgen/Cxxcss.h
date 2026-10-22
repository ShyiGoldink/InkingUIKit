#pragma once

// CXXCSS 的**加载 + 校验**：一个 json → 一份运行期配置（`inking::ButtonData` /
// `inking::PanelData` / `inking::LabelData` / `inking::TextTable`）。
//
// 这一层直接产出**运行期那些结构体**，不另立一套"生成器自己的配置类型"。
// 理由：两套类型意味着每个字段有两份定义、两处校验、两条会分叉的演化路径
// ——而它们表达的本来就是同一件事。
//
// **组件类型由目录名决定**，不由配置里的字段决定（`CXXCSS.md` §1 第 1 条
// "目录名 = 组件类型"）：`<...>/Button/x.json` 是按钮、`<...>/Panel/x.json`
// 是面板、`<...>/Label/x.json` 是标签、`<...>/Text/x.json` 是**语言表**。
// 这样"加一个组件类型"= 生成器里加一条目录名映射 + 一个 Loader，
// 而不是让每份配置都多写一个 `type` 字段（那个字段迟早会和目录名打架）。
//
// 校验的调子按 CXXCSS.md §3.6 定：**报错停下，不是夹一下就过**。
// 生成器阶段的问题最容易查，放进运行期就是"跑起来了但样子不对"。
// 唯一的例外是形状的 radius 上限（那是形状本身的数学要求，运行期自己夹）。

#include "Json.h"

#include <button/ButtonData.h>
#include <image/ImageData.h>
#include <panel/PanelData.h>
#include <text/InkingTextTable.h>
#include <text/LabelData.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace inkgen {

/// 一条校验错误：指到文件、行号、说清哪里不对。
struct CxxcssError {
    std::string file;
    int line = 0;
    std::string message;

    /// "CXXCSS/Button/x.json:12: 字段 \"foo\" 不认识" —— 编辑器能直接跳转的格式。
    std::string Format() const;
};

/**
 * 配置属于哪种组件。**由目录名决定**（见文件头顶上那段）。
 *
 * 加新组件类型时这里加一项、`TryComponentKindFromPath` 加一条映射、
 * 再写一个 Loader——`Emit` 那边也对应加一段。
 */
enum class ComponentKind {
    Button = 0,
    Panel,
    Label,  ///< 静态标签（A7）：一段只读文字，按内容撑高
    Image,  ///< **静态图片**（A11）：不带点击回调的按钮（数据同构）
    Text,   ///< **语言表**（A7）：占位符 → 各语言的串。它不是组件，是全局表
    Theme,  ///< **主题表**（B8）：语义色 / 语义字号 → 具体值。它也不是组件
    Config, ///< **全局配置**：设计画布尺寸（letterbox 的逻辑坐标系）。也不是组件
};

/**
 * 从路径推组件类型：看**父目录名**，大小写不敏感、允许复数。
 *
 * 认得的写法：`Button` / `button` / `buttons` / `BUTTONS` → Button；
 * `Panel` / `panels` → Panel；`Label` / `labels` → Label；
 * `Image` / `images` → Image（**静态图片**，A11）；
 * `Text` / `texts` → Text（**语言表**，不是"文字组件"）；
 * `Theme` / `themes` → Theme（**主题表**，不是组件）；
 * `Config` / `configs` → Config（**全局配置**，不是组件）。
 *
 * 全局配置还认一种位置：**直接躺在 `CXXCSS/` 根下**（父目录名就是 `CXXCSS`）——
 * 它是整个工程唯一的一份设置，"先建个文件夹再往里放"在这里只是多余的一层。
 *
 * 其它目录名返回 false——调用方要**报错**（报错文案里给出认得的全部写法），
 * 不能悄悄按 Button 处理：那会让"Panel 配置放错目录"表现成一堆莫名其妙的字段报错。
 */
bool TryComponentKindFromPath(const std::string& path, ComponentKind& out);

/// 组件类型的中文名（报错与日志用）："按钮" / "面板" / "标签" / "图片" /
/// "语言表" / "主题表" / "全局配置"。
const char* ComponentKindName(ComponentKind kind);

/// 一份加载成功的按钮配置。
struct ButtonConfig {
    std::string filePath;          ///< 来源文件，报错和日志要用
    std::string name;              ///< 配置里的 name（= ButtonLibrary 的查找键）
    /**
     * 生成出来的**类名**（`normalButton` → `NormalButton`）。
     *
     * name 不是合法 C++ 标识符时为空——那时生成不出类，只能按字符串名字使用。
     * 类名同时是"这个按钮的类型"和"拼错名字的编译期保险"：
     * 数据烘在类里，没有运行期查表。
     */
    std::string identifier;
    /**
     * 生成成**动态**按钮吗（json 里的 `"dynamic": true`）。
     *
     * 这一点只决定"生成出来的类继承谁"：静态继承 `inking::InkingStaticButton`、
     * 动态继承 `inking::InkingDynamicButton`（几何可以在运行期改，代价是不进命中表，
     * 指针由它自己在 Tick 里拉——见 include/button/InkingDynamicButton.h）。
     *
     * 它**不进运行期那份数据结构**（`inking::ButtonData` 里没有这个字段）：
     * 类型本身已经说明了它属于哪一档，再存一遍就是第二份真相，
     * 而第二份真相迟早会和第一份不一致。
     */
    bool dynamic = false;
    inking::ButtonData data{};        ///< 运行期那份配置

    /// `$extends` 的来源（相对 CXXCSS 根、不带 .json；空 = 没继承）。只用于
    /// 生成物注释——合并发生在读配置那一层，运行期数据结构里没有这项。
    std::string extendsFrom;

    /**
     * `img` 外观的**图像字节**（T-4）：生成器在生成期把文件读进来，`Emit` 会把它
     * 烘成生成代码里的静态数组。下标 0 / 1 / 2 = normal / hover / onclicked，
     * 空表示那个状态不是 img（或者读文件时已经报错）。
     *
     * 为什么放在 ButtonConfig 而不是 `ButtonData`：`ButtonData` 是**运行期**结构，
     * 里面只有"指向字节的视图"；字节本体属于生成物，生成器这一侧得先拿住它。
     */
    std::vector<std::uint8_t> imageBytes[3];

    /**
     * `polygonPath` 形状的**顶点表**（B2）：`x0,y0,x1,y1,…`，设计坐标。
     *
     * 和 `imageBytes` 同一个道理——`ShapeSpec` 里只有"指向点列的视图"，
     * 本体属于生成物，生成器这一侧得先拿住它，再由 `Emit` 烘成静态数组。
     * 解析完在 `Finish()` 里把 `data.shape.pathPoints` 指回这里（见那里的注释）。
     */
    std::vector<float> pathPoints;

    /// 非致命提醒（例如 `text.content.hover` 与 normal 不同）。不算错误，但要让用户看见。
    std::vector<std::string> warnings;
};

/**
 * 一份加载成功的面板配置。
 *
 * 它比 `ButtonConfig` 简单：面板没有三态、没有文字、没有 `img` / `svg` 外观，
 * 所以没有图像字节要烘、也不需要"选基类"的开关（面板只有静态档——开合只改变换，
 * 几何构造即定型）。
 */
struct PanelConfig {
    std::string filePath;
    std::string name;
    std::string identifier;   ///< 生成类名（`drawerPanel` → `DrawerPanel`）
    inking::PanelData data{};

    /// `$extends` 的来源（相对 CXXCSS 根、不带 .json；空 = 没继承）。只用于
    /// 生成物注释——合并发生在读配置那一层，运行期数据结构里没有这项。
    std::string extendsFrom;
    std::vector<std::string> warnings;
};

/**
 * 一份加载成功的标签配置（`CXXCSS/Label/<名字>.json`，A7 的生成器段）。
 *
 * 它比 `PanelConfig` 还简单：标签是**叶子**——没有三态、没有开合、没有形状
 * （`LabelData` 里就没有 `shape` 字段：标签只画文字），文字怎么排全在
 * `text` 段里（内容 + 样式 + 换行 / 省略）。
 */
struct LabelConfig {
    std::string filePath;
    std::string name;
    std::string identifier;   ///< 生成类名（`menuTitle` → `MenuTitle`）
    inking::LabelData data{};

    /// `$extends` 的来源（相对 CXXCSS 根、不带 .json；空 = 没继承）。只用于
    /// 生成物注释——合并发生在读配置那一层，运行期数据结构里没有这项。
    std::string extendsFrom;
    std::vector<std::string> warnings;
};

/**
 * 一份加载成功的**图片**配置（`CXXCSS/Image/<名字>.json`，A11）。
 *
 * 数据就是 `inking::ImageData`——它是 `inking::ButtonData` 的派生（多一个
 * `runtimeAsset` 开关）。所以生成器这边**逐字复用**按钮那套读取与烘法：
 * 图片配置认的字段 = 按钮那套**减去三态 / 文字 / 过渡**（只有 `normal` 一态），
 * 加上 `runtime` 那个开关。
 *
 * 两种"图从哪来"（这与 A11 的目的直接相关）：
 *   - `runtime` 省略 / false：`normal.source` 是**绝对路径**，生成期把文件字节
 *     烘成静态数组（照旧行为，exe 自带图）；读不到文件就是生成期报错；
 *   - `"runtime": true`：`normal.source` 是**相对资源根**的路径（默认 exe 旁的
 *     `assets/`），**生成期一个字都不读**、产物里只有那个路径字符串，运行期由
 *     `inking::AssetLoad` 读（可带轻量混淆）。这是"别把所有图烘进 exe"那条路。
 */
struct ImageConfig {
    std::string filePath;
    std::string name;
    std::string identifier;  ///< 生成类名（`cardArt` → `CardArt`）

    /**
     * **复用按钮整份配置**（外观 / 形状 / 锚点 / 生成期烘好的字节 / 顶点表）。
     *
     * 图片就是"没有 onClicked 的 button"（A11 的原话），数据同构 ⇒ 生成器这边
     * 逐字复用 `ButtonBody` 那套烘法（只把类型名换成 `inking::ImageData`、末尾补一行
     * `runtimeAsset`）。另一条路是"再抄一份字段并同步维护"，那是
     * `AGENTS.md` §6 第 83 条记的那种坑。
     */
    ButtonConfig button;

    /// `$extends` 的来源（相对 CXXCSS 根、不带 .json；空 = 没继承）。只用于
    /// 生成物注释——合并发生在读配置那一层，运行期数据结构里没有这项。
    std::string extendsFrom;

    /// 配置里的 `runtime`：true = 字节在**运行期**从资源根读（不进 exe）。
    bool runtimeAsset = false;

    /// 配置里的 `normal` 段见过没有。
    bool sawNormal = false;

    std::vector<std::string> warnings;
};

/**
 * 一份加载成功的**语言表**配置（`CXXCSS/Text/<名字>.json`）。
 *
 * 它不是组件：`Text` 目录描述的是一张**全局表**（占位符 → 各语言的串），
 * 运行期由 `inking::InstallTextTable` 装进 `include/text/InkingTextTable.h` 那份
 * 状态里。所以它没有尺寸、没有锚点、也不进命中表 / 绘制列表。
 *
 * **只允许一份**：`InstallTextTable` 的语义就是"替换整张表"，装两张只有一张
 * 生效——与其让用户猜哪张赢，不如在生成期直接报错（`LoadConfigInto` 那条路）。
 *
 * `defaultLanguage` 是 json 里可选的 `default`（必须是 `table.languages` 里的
 * 一门）；为空表示"用表里第一门"（与 `InstallTextTable` 的缺省一致）。
 */
struct TextConfig {
    std::string filePath;
    std::string name;              ///< 配置里的 name（= 文件名）
    std::string identifier;        ///< 生成用的名字（`uiStrings` → `UiStrings`）
    std::string defaultLanguage;   ///< 空 = 表里第一门
    inking::TextTable table{};
    std::vector<std::string> warnings;
};

/**
 * @brief 一份加载成功的**主题表**配置（`CXXCSS/Theme/<名字>.json`，B8）。
 *
 * 它**不是组件**，而是**生成器的输入**：组件配置里写 `"source": "$surface"` /
 * `"fontSize": "$body"` 这样的**语义名**，生成器在生成期把它换成主题里的具体值、
 * 照旧烘成字面量。所以产物里**没有**主题类、也没有运行期查表——
 * 换主题 = 改主题表（或换一份）之后**重新生成**。
 *
 * 取舍写在 `CXXCSS.md` §3.9：**生成期解析**让"改一处、所有组件跟着变"成立，
 * 又不动运行期数据结构（颜色仍是 `uint32`、字号仍是 `float`）；
 * 代价是换肤要重编。运行期换肤要"数据里保留 token + 逐组件查表 + 版本号"，
 * 那是它之后的第二版（`TaskGuide.md` 的 B8 那条写明了边界）。
 *
 * **只允许一份**（同 `Text`）：两份主题只有一份生效，与其让人猜哪份赢，
 * 不如生成期报错。
 */
struct ThemeConfig {
    std::string filePath;
    std::string name;  ///< 配置里的 name（= 文件名）

    /// 多份主题时用它指定**烘焙主题**（`$token` 按它解析成字面量）。
    bool isDefault = false;

    /// 颜色表：token → 0xAARRGGBB（保序，报错时按书写顺序列出来）。
    std::vector<std::pair<std::string, std::uint32_t>> colors;

    /// 字号表：token → 字号（设计坐标，必须大于 0）。
    std::vector<std::pair<std::string, double>> fontSizes;

    std::vector<std::string> warnings;

    /// 查一个颜色 token；没有返回 `nullptr`。
    const std::uint32_t* FindColor(const std::string& token) const noexcept;

    /// 查一个字号 token；没有返回 `nullptr`。
    const double* FindFontSize(const std::string& token) const noexcept;
};

/**
 * @brief 一份加载成功的**全局配置**（`CXXCSS/config.json`）。
 *
 * 它**不是组件**，产物里也没有类：只有几对常量 + 一个 `ApplyConfig()`。
 *
 * 两块内容：
 *   - `design`（必填）：设计画布尺寸，决定 letterbox 的逻辑坐标系——**所有**
 *     锚点、尺寸、字体、间距都写在这个坐标系里，所以它必须先于一切几何成立；
 *   - `window`（可选）：窗口启动设置（无边框 / 圆角 / 清底 / 全屏 / 起始场景）。
 *
 * 为什么产物是"常量 + 一次调用"而不是"只生成一个头文件让库自己读"：
 * 本库以**预编译 DLL** 分发，配置改的是消费方的构建产物，碰不到 DLL 里烘死的
 * 编译期常量。所以这些设置必须有**运行期**入口（`inking::SetDesignSize` /
 * `inking::ApplyWindowConfig`），生成代码里的 `ApplyConfig()` 就是它的唯一正确用法。
 *
 * `window` 的每一项都是 `std::optional`：**没写 = 不动**（保持库里默认值）。
 * 不在这里抄一份"默认值"——抄了就成了第二份真相，库改默认值时它一定是错的。
 *
 * **只允许一份**（同语言表 / 主题表）：两份配置只有一份生效。
 */
struct GlobalConfig {
    std::string filePath;

    /// 设计画布（`design.width/height`，必填，1..kMaxDesignSize 的整数）。
    int width  = 0;
    int height = 0;

    /// 以下都来自可选的 `window` 段；空 = json 没写 = 不动。
    std::optional<bool> borderless;
    std::optional<bool> roundedCorners;
    std::optional<std::uint32_t> background;  ///< 已解析成 0xAARRGGBB（`$token` 也支持）
    std::optional<bool> fullscreen;
    std::optional<std::string> startScene;    ///< 只是常量：`Show()` 的参数值

    std::vector<std::string> warnings;
};

/**
 * @brief 一次加载的结果：成功的配置 + 全部错误。
 *
 * 错误**不早退**：一次把文件里所有问题都列出来，用户改一轮就能全改完，
 * 而不是"改一条、再跑一次、又冒一条"。
 */
struct LoadResult {
    std::vector<ButtonConfig> buttons;
    std::vector<PanelConfig> panels;
    std::vector<LabelConfig> labels;
    std::vector<ImageConfig> images;
    std::vector<TextConfig> textTables;

    /**
     * 主题表（可以多份）。
     *
     * **第一份是"烘焙主题"**：组件里的 `$token` 在加载那一刻就按它解析成字面量，
     * 也就是产物里的默认值。其余几份只有 token 名有意义——运行期换肤时按名字
     * 重查（`theme/InkingTheme.h`）。
     *
     * 它们**必须先装好**：组件的 `$token` 在加载那一刻就要解析，所以
     * `LoadConfigs` 是**两趟**——第一趟收主题、第二趟收组件（见那里的注释）。
     */
    std::vector<ThemeConfig> themes;

    /**
     * 组件里真正用到的颜色 token（去重，按首次出现顺序）。
     *
     * 用来做"**每张主题表都必须定义这些 token**"的跨表校验：少一个的表现是
     * "切到那张主题后，这一处还是旧色"——生成期能抓，就别留到运行期。
     */
    std::vector<std::string> usedColorTokens;

    /**
     * token 名的驻留池。
     *
     * 生成的 `themeColor` 是 `const char*`，指向这里——`std::deque` 保证
     * `push_back` 不会让已有元素失效，所以指针在整个加载 + 发射期间都有效
     * （换 `std::vector` 会在扩容时把指针全变成野的）。
     */
    std::deque<std::string> internedTokens;

    /// 全局配置（最多一份）。没有它就用库里的默认设计尺寸（1280×720）与窗口默认值。
    std::vector<GlobalConfig> configs;

    std::vector<CxxcssError> errors;

    /**
     * 本次加载里每个文件认领的名字（名字 + 来源文件）。
     *
     * **跨组件类型共用一份**：名字是运行期的全局查找键，一个按钮和一个面板
     * 叫同一个名字同样是错——所以重名检查必须看得见"别的类型认领了什么"。
     *
     * 不过滤"有没有通过校验"：一个文件可能因为别的问题被拒绝、因而不进
     * `buttons` / `panels`，但它认领了某个名字这件事仍然成立。
     */
    std::vector<std::pair<std::string, std::string>> claimedNames;

    /// 这次加载出多少个**组件**（按钮 + 面板 + 标签 + 图片）。语言表 / 主题表不算。
    std::size_t ComponentCount() const {
        return buttons.size() + panels.size() + labels.size() + images.size();
    }

    bool Ok() const { return errors.empty(); }
};

/// 加载并校验一个 `CXXCSS/<组件类型>/<名字>.json`。
LoadResult LoadConfig(const std::string& path);

/**
 * @brief 加载一个文件，结果**追加**到 `result` 上。
 *
 * 批量加载必须走这个（而不是"每个文件各加载一份再合并"）：重名检查要看的是
 * **到目前为止这一批**认领了哪些名字，各自为政就会让"第二个文件撞上第一个"
 * 检不出来——那个 bug 真的踩过。
 */
void LoadConfigInto(const std::string& path, LoadResult& result);

/**
 * @brief 加载一批文件；`*.example.json` 会被**跳过**（约定见 CXXCSS.md §1）。
 *
 * 显式传进来的 `.example.json` 会被跳过并记一条提示——批量收文件时它是正常的，
 * 但用户单独指着它说"生成这个"时，跳过比静默忽略更安全。
 */
LoadResult LoadConfigs(const std::vector<std::string>& paths);

/// 文件名（不含目录、不含 `.json`）——用来核对 `name` 字段（CXXCSS.md §1 第 2 条）。
std::string FileStem(const std::string& path);

/// `"normalButton"` → `"NormalButton"`（类名）。不是合法标识符时返回空串。
std::string MakeNameConstant(const std::string& name);

/// 名字是不是合法的 C++ 标识符（不以数字开头、只含字母数字下划线）。
bool IsCxxIdentifier(const std::string& name);

}  // namespace inkgen
