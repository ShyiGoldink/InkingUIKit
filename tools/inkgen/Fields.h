#pragma once

// 生成器认的**全部字段名**的单一来源（0.2.0 T-08）。
//
// 为什么要有这个文件：白名单原来散落在 `Cxxcss.cpp` 各处的数组里，"报错里有、
// 文档里没有"就成了常态——反馈者要查"Panel 认哪些字段"，只能跑一次
// `--validate` 故意写错，再从报错里抄。现在：
//   1. 数组集中在这里（`kKnownXxxFields`）；
//   2. `inkgen --dump-fields` 按 `kFieldGroups` 输出三层表（组件 × 段 × 字段）；
//   3. `CXXCSS.md` 的 §9 锚点块由 dump 填充，ctest `field_table_in_sync` 比对
//      不一致就红——**新增字段忘记同步文档 = 测试失败**。
//
// ⚠️ **数组顺序一个字都不能动**：报错文案由 `JoinKnownFields` 按数组顺序拼
// （"已知字段：a、b、c"），既有自检断言依赖那段文案；调顺序 = 改文案。
//
// 三层的意义：存在"同名不同义"的字段——Button 的 `text.leftSpace` 与
// Label 的 `text.align`、以及 `text` 这个段名在两个组件里装的东西完全不同。
// 两层表（字段 → 说明）表达不了这件事，所以必须有"段"这一层。

#include <cstddef>
#include <string>

namespace inkgen::fields {

// ---------------------------------------------------------------------------
// 组件顶层
// ---------------------------------------------------------------------------

/// Button 顶层字段（`CXXCSS.md` §4）。
inline constexpr const char* const kKnownRootFields[] = {
    "$comment",    "name",        "width",        "height",
    "normal",      "hover",       "onclicked",    "text",
    "selfAnchorX", "selfAnchorY", "traceAnchorX", "traceAnchorY",
    "offsetX",     "offsetY",     "zIndex",       "visible",
    "shape",       "dynamic",     "stroke",       "shadow",
    "layout",      "layoutItem",
};

/// Panel 顶层字段（`CXXCSS.md` §4.4）。
inline constexpr const char* const kKnownPanelFields[] = {
    "$comment",    "name",         "width",        "height",
    "closed",      "open",         "startOpen",    "clipChildren", "scroll",
    "selfAnchorX", "selfAnchorY",  "traceAnchorX", "traceAnchorY",
    "offsetX",     "offsetY",      "zIndex",       "visible",
    "shape",       "layout",       "layoutItem",
};

/// Label 顶层字段（`CXXCSS.md` §3.8）。
inline constexpr const char* const kKnownLabelFields[] = {
    "$comment",     "name",        "width",        "height",
    "text",         "selfAnchorX", "selfAnchorY",  "traceAnchorX",
    "traceAnchorY", "offsetX",     "offsetY",      "zIndex",
    "visible",      "layout",      "layoutItem",
};

/// Image 顶层字段（`CXXCSS.md` §3.10）。
inline constexpr const char* const kKnownImageFields[] = {
    "$comment",   "name",         "width",        "height",  "normal",
    "runtime",    "shape",        "stroke",       "shadow",  "selfAnchorX",
    "selfAnchorY", "traceAnchorX", "traceAnchorY", "offsetX", "offsetY",
    "layoutItem", "zIndex",       "visible",      "hitPassThrough",
};

/// 语言表字段（不是组件，是全局表）。
inline constexpr const char* const kKnownTextTableFields[] = {
    "$comment", "name", "languages", "default", "entries",
};

/// 主题表字段（生成器的输入）。
inline constexpr const char* const kKnownThemeFields[] = {
    "$comment", "name", "colors", "fontSizes", "default",
};

/// 全局配置顶层字段。
inline constexpr const char* const kKnownConfigFields[] = {
    "$comment", "name", "design", "window",
};

/// 全局配置 `design` 段。
inline constexpr const char* const kKnownDesignFields[] = {
    "$comment", "width", "height",
};

/// 全局配置 `window` 段（每项都对应一对有 getter 的 setter）。
inline constexpr const char* const kKnownWindowFields[] = {
    "$comment", "borderless", "roundedCorners", "transparent", "background", "fullscreen",
    "startScene",
};

// ---------------------------------------------------------------------------
// 段级（嵌套对象里的字段）
// ---------------------------------------------------------------------------

/// Button 的 `text` 段。
inline constexpr const char* const kKnownTextFields[] = {
    "fontSize", "leftSpace", "topSpace", "path", "content", "align", "alignV",
};

/// Label 的 `text` 段（与 Button 的 `text` **同名不同义**：这里是整块排版）。
inline constexpr const char* const kKnownLabelTextFields[] = {
    "content", "fontSize", "lineSpacing", "path",   "align",
    "alignV",  "padding",  "color",       "wrap",   "overflow",
    "maxLines",
};

/// `shape` 对象里的字段（所有带形状的组件共用）。
inline constexpr const char* const kKnownShapeFields[] = {
    "type",     "radius",     "thickness",  "cap",       "sides",
    "rotation", "chamfer",    "points",     "innerRatio", "exponent",
    "startAngle", "sweepAngle", "vertices",
};

/// Button 三态外观（`normal` / `hover` / `onclicked`）里的字段。
inline constexpr const char* const kKnownButtonAppearanceFields[] = {
    "type", "source", "transition", "transform", "width", "height", "fit",
};

/// Panel 两态外观（`closed` / `open`）里的字段。
inline constexpr const char* const kKnownPanelAppearanceFields[] = {
    "type", "source", "transition", "transform", "stroke", "shadow",
};

/// Image 的 `normal` 外观里的字段（没有三态 / 过渡 / 逐态尺寸）。
///
/// `svg` 是**纹理路**：整份 SVG 在运行期按目标像素光栅化（0.5.1），与 Button 的
/// `svg`（生成期转成形状、只支持单图形）**同名不同义**。
inline constexpr const char* const kKnownImageAppearanceFields[] = {
    "type", "source", "fit",
};

/// `transition` 段。
inline constexpr const char* const kKnownTransitionFields[] = {
    "duration", "easing",
};

/// `stroke` 段（与 `shape` 平级的两笔装饰之一）。
inline constexpr const char* const kKnownStrokeFields[] = {
    "width", "color",
};

/// `shadow` 段（**没有 blur**：现在画的是偏移实心副本）。
inline constexpr const char* const kKnownShadowFields[] = {
    "offsetX", "offsetY", "color",
};

// ---------------------------------------------------------------------------
// dump 表：组件 × 段 × 字段
// ---------------------------------------------------------------------------

/// 一组字段（一个组件的一个段）。
struct FieldGroup {
    const char* component;  ///< 组件 / 表名（"Button" / "Theme" / ...）
    const char* section;    ///< 段名；"（顶层）" 表示组件配置的最外层
    const char* const* names;
    std::size_t count;
    /// 段级的一句说明（同组字段共用）——写清"这一段装什么、和谁容易混"。
    const char* note;
};

/// 表的顺序 = dump 的顺序（人工排的：组件 → 由外到内 → 全局表）。
inline constexpr FieldGroup kFieldGroups[] = {
    {"Button", "（顶层）", kKnownRootFields, std::size(kKnownRootFields),
     "组件级：三态外观、文字段、锚点、布局、装饰"},
    {"Button", "normal / hover / onclicked", kKnownButtonAppearanceFields,
     std::size(kKnownButtonAppearanceFields),
     "三态外观；逐态还能改尺寸（width / height，T-5 的布局通道）"},
    {"Button", "text", kKnownTextFields, std::size(kKnownTextFields),
     "按钮上的文字段：位置靠 leftSpace / topSpace（不是 Label 的 align）"},
    {"Panel", "（顶层）", kKnownPanelFields, std::size(kKnownPanelFields),
     "容器级：两态外观 + 开合 / 裁剪 / 滚动"},
    {"Panel", "closed / open", kKnownPanelAppearanceFields,
     std::size(kKnownPanelAppearanceFields),
     "两态外观；open 省略时颜色 / 描边 / 阴影继承 closed（过渡与变换不继承）"},
    {"Label", "（顶层）", kKnownLabelFields, std::size(kKnownLabelFields),
     "标签没有 shape / stroke / shadow：它只画文字、不画底"},
    {"Label", "text", kKnownLabelTextFields, std::size(kKnownLabelTextFields),
     "标签的文字段：整块排版（换行 / 省略 / 行距），与 Button 的 text 同名不同义"},
    {"Image", "（顶层）", kKnownImageFields, std::size(kKnownImageFields),
     "静态图片 = 没有 onClicked 的 button：没有三态、没有 dynamic；hitPassThrough = 背景不吃点击"},
    {"Image", "normal", kKnownImageAppearanceFields,
     std::size(kKnownImageAppearanceFields),
     "图片外观认 color / img / svg；svg 是纹理路（整份稿光栅化），与按钮的 svg 不同"},
    {"Text", "（顶层）", kKnownTextTableFields, std::size(kKnownTextTableFields),
     "语言表（不是组件）：占位符 → 各语言的串，只允许一份"},
    {"Theme", "（顶层）", kKnownThemeFields, std::size(kKnownThemeFields),
     "主题表：生成器的输入，$token 在生成期换成字面量"},
    {"Config", "（顶层）", kKnownConfigFields, std::size(kKnownConfigFields),
     "全局配置：设计画布 + 窗口启动设置（只允许一份）"},
    {"Config", "design", kKnownDesignFields, std::size(kKnownDesignFields),
     "设计画布尺寸（必填）"},
    {"Config", "window", kKnownWindowFields, std::size(kKnownWindowFields),
     "窗口启动设置；每项都对应一对有 getter 的 setter"},
    {"（所有组件）", "shape", kKnownShapeFields, std::size(kKnownShapeFields),
     "形状参数（几何）；描边 / 阴影与它平级，别塞进来"},
    {"（所有组件）", "transition", kKnownTransitionFields,
     std::size(kKnownTransitionFields),
     "过渡：进这个状态用多久（时长 + 缓动曲线）"},
    {"（所有组件）", "stroke", kKnownStrokeFields, std::size(kKnownStrokeFields),
     "描边：宽度一半在外一半在内；面板的描边画在子孙之上"},
    {"（所有组件）", "shadow", kKnownShadowFields, std::size(kKnownShadowFields),
     "阴影：形状的偏移实心副本（无模糊，所以不认 blur）"},
};

/// 生成 Markdown 表（`| 组件 | 段 | 字段 | 说明 |`）。
///
/// 说明列只在该组第一行写（逐行重复同一句话只是噪声）；字段名带反引号，
/// 因为它可能以 `$` 开头（`$comment`），不加反引号在 Markdown 里会被当成
/// 别的东西。
inline std::string DumpFieldTable() {
    std::string out;
    out += "| 组件 | 段 | 字段 | 说明 |\n";
    out += "| --- | --- | --- | --- |\n";
    for (const FieldGroup& group : kFieldGroups) {
        for (std::size_t i = 0; i < group.count; ++i) {
            out += "| ";
            out += group.component;
            out += " | ";
            out += group.section;
            out += " | `";
            out += group.names[i];
            out += "` | ";
            if (i == 0) {
                out += group.note;
            }
            out += " |\n";
        }
    }
    return out;
}

}  // namespace inkgen::fields
