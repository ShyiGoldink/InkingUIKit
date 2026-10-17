#include "Emit.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace inkgen {

namespace {

/// 浮点字面量：整数值也写成 `10.0f`，免得在 C++ 里变成 int 再隐式转换。
std::string Float(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6ff", value);
    return buffer;
}

/// 布局字段（A6）的赋值块。定义在下面——这里先声明一次，
/// `ButtonBody` 与 `PanelBody` 都能用（它们在定义之前）。
std::string LayoutAssignments(const inking::LayoutSpec& spec,
                              const inking::LayoutItem& item,
                              const std::string& indent,
                              const std::string& specTarget,
                              const std::string& itemTarget);

/// 0xAARRGGBB → C++ 字面量。
std::string Color(std::uint32_t argb) {    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08Xu", argb);
    return buffer;
}

/// 带引号的字符串字面量（转义反斜杠和双引号；UTF-8 中文原样留着）。
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(ch); break;
        }
    }
    out += "\"";
    return out;
}

/// 图像字节数组的名字。标识符非法（生成不出类）时退回按下标命名。
std::string ImageArrayName(const ButtonConfig& button, int state,
                           std::size_t index) {
    const char* stateName =
        state == 0 ? "Normal" : (state == 1 ? "Hover" : "Onclicked");
    if (button.identifier.empty()) {
        return "kImage" + std::to_string(index) + stateName;
    }
    return "k" + button.identifier + "Image" + stateName;
}

/**
 * `polygonPath` 的**顶点表数组名**（B2）。
 *
 * 和图像字节同一套命名规则：标识符非法（生成不出类）时按下标命名。
 */
std::string PathArrayName(const ButtonConfig& button, std::size_t index) {
    if (button.identifier.empty()) {
        return "kPath" + std::to_string(index);
    }
    return "k" + button.identifier + "Path";
}

/// `inking::Easing` 的枚举名——生成代码里要写出 `inking::Easing::EaseOut` 这种字面量。
const char* EasingIdentifier(inking::Easing easing) {
    switch (easing) {
        case inking::Easing::EaseIn:
            return "EaseIn";
        case inking::Easing::EaseOut:
            return "EaseOut";
        case inking::Easing::EaseInOut:
            return "EaseInOut";
        case inking::Easing::Linear:
        default:
            return "Linear";
    }
}

/// `inking::LineCap` 的枚举名——生成代码里要写出 `inking::LineCap::Round` 这种字面量。
const char* LineCapIdentifier(inking::LineCap cap) {
    switch (cap) {
        case inking::LineCap::Round:
            return "Round";
        case inking::LineCap::Square:
            return "Square";
        case inking::LineCap::Butt:
        default:
            return "Butt";
    }
}

/// `inking::TextAlignH` 的枚举名（A7 的标签排版）。
const char* TextAlignHIdentifier(inking::TextAlignH align) {
    switch (align) {
        case inking::TextAlignH::Center:
            return "Center";
        case inking::TextAlignH::Right:
            return "Right";
        case inking::TextAlignH::Left:
        default:
            return "Left";
    }
}

/// `inking::TextAlignV` 的枚举名。
const char* TextAlignVIdentifier(inking::TextAlignV align) {
    switch (align) {
        case inking::TextAlignV::Middle:
            return "Middle";
        case inking::TextAlignV::Bottom:
            return "Bottom";
        case inking::TextAlignV::Top:
        default:
            return "Top";
    }
}

/// `inking::TextWrap` 的枚举名。
const char* TextWrapIdentifier(inking::TextWrap wrap) {
    switch (wrap) {
        case inking::TextWrap::NoWrap:
            return "NoWrap";
        case inking::TextWrap::Anywhere:
            return "Anywhere";
        case inking::TextWrap::Word:
        default:
            return "Word";
    }
}

/// `inking::TextOverflow` 的枚举名。
const char* TextOverflowIdentifier(inking::TextOverflow overflow) {
    switch (overflow) {
        case inking::TextOverflow::Ellipsis:
            return "Ellipsis";
        case inking::TextOverflow::Clip:
        default:
            return "Clip";
    }
}

std::vector<std::string> SplitNamespace(const std::string& nameSpace) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : nameSpace) {
        if (ch == ':') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

/// `inking::cxxcss::RegisterAllButtons` —— 生成代码里引用自己的完整名字时用。
std::string Qualified(const std::string& nameSpace, const std::string& symbol) {
    return nameSpace + "::" + symbol;
}

std::string OpenNamespaces(const std::vector<std::string>& parts,
                           int indentSpaces = 0) {
    std::string out;
    std::string indent(static_cast<std::size_t>(indentSpaces), ' ');
    for (const std::string& part : parts) {
        out += indent + "namespace " + part + " {\n";
    }
    return out;
}

std::string CloseNamespaces(const std::vector<std::string>& parts,
                            int indentSpaces = 0) {
    std::string out;
    std::string indent(static_cast<std::size_t>(indentSpaces), ' ');
    for (std::size_t i = 0; i < parts.size(); ++i) {
        out += indent + "}  // namespace " + parts[parts.size() - 1 - i] + "\n";
    }
    return out;
}

/// 缩进多行文本；空行不缩进（免得行尾留空格）。
std::string Indent(const std::string& text, int spaces) {
    const std::string pad(static_cast<std::size_t>(spaces), ' ');
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = (end == std::string::npos)
                                     ? text.substr(start)
                                     : text.substr(start, end - start);
        if (!line.empty()) {
            out += pad;
        }
        out += line;
        if (end == std::string::npos) {
            break;
        }
        out += '\n';
        start = end + 1;
    }
    return out;
}

/**
 * 形状字段的赋值语句。
 *
 * 按钮与面板的形状来自**同一套形状层**（`CXXCSS.md` §3），所以这段分支只该有
 * 一份——抄第二遍就会出现"按钮认 capsule、面板不认"这种分叉，而它只在某个
 * 组件上表现成"配置写了没生效"。
 *
 * @param shape  要写出来的形状
 * @param indent 行首缩进
 * @param target 赋值目标（`data.shape` 之类）
 */
std::string ShapeAssignment(const inking::ShapeSpec& shape,
                            const std::string& indent,
                            const std::string& target,
                            const std::string& pathArray = {}) {
    std::ostringstream out;
    switch (shape.kind) {
        case inking::ShapeKind::RoundedRect:
            out << indent << target << " = inking::ShapeSpec::RoundedRect("
                << Float(shape.radius) << ");\n";
            break;
        case inking::ShapeKind::Circle:
            out << indent << target << " = inking::ShapeSpec::Circle();\n";
            break;
        case inking::ShapeKind::Ellipse:
            out << indent << target << " = inking::ShapeSpec::Ellipse();\n";
            break;
        case inking::ShapeKind::Capsule:
            out << indent << target << " = inking::ShapeSpec::Capsule();\n";
            break;
        case inking::ShapeKind::Ring:
            out << indent << target << " = inking::ShapeSpec::Ring("
                << Float(shape.thickness) << ");\n";
            break;
        case inking::ShapeKind::Line:
            out << indent << target << " = inking::ShapeSpec::Line("
                << Float(shape.thickness) << ", inking::LineCap::"
                << LineCapIdentifier(shape.cap) << ");\n";
            break;
        case inking::ShapeKind::Polygon:
            out << indent << target << " = inking::ShapeSpec::Polygon("
                << shape.sides << ", " << Float(shape.rotation) << ");\n";
            break;
        case inking::ShapeKind::ChamferRect:
            out << indent << target << " = inking::ShapeSpec::ChamferRect("
                << Float(shape.chamfer) << ");\n";
            break;
        case inking::ShapeKind::Star:
            out << indent << target << " = inking::ShapeSpec::Star(" << shape.points
                << ", " << Float(shape.innerRatio) << ", "
                << Float(shape.rotation) << ");\n";
            break;
        case inking::ShapeKind::Superellipse:
            out << indent << target << " = inking::ShapeSpec::Superellipse("
                << Float(shape.exponent) << ");\n";
            break;
        case inking::ShapeKind::Arc:
            out << indent << target << " = inking::ShapeSpec::Arc("
                << Float(shape.thickness) << ", " << Float(shape.startAngle)
                << ", " << Float(shape.sweepAngle) << ");\n";
            break;
        case inking::ShapeKind::PolygonPath:
            // 点列烘成文件作用域的静态数组（生命周期是整个进程），这里只传名字
            // ——`ShapeSpec` 里存的是视图（见 `InkingShapeSpec.h` 的字段注释）。
            out << indent << target << " = inking::ShapeSpec::PolygonPath("
                << (pathArray.empty() ? "nullptr" : pathArray) << ", "
                << shape.pathPointCount << ");\n";
            break;
        case inking::ShapeKind::Rect:
        default:
            out << indent << target << " = inking::ShapeSpec::Rect();\n";
            break;
    }
    return out.str();
}

/**
 * 一份配置 → 一个"造 ButtonData"的表达式块。
 *
 * 用**逐字段赋值**而不是聚合初始化 `{...}`：字段多、顺序敏感，聚合初始化一旦
 * 有人插了新成员就会静默错位（值都还在，只是落到了别的字段上）。
 * 赋值写法还顺手给生成代码加了可读性——看输出就知道哪个值对应哪个字段。
 */
std::string ButtonBody(const ButtonConfig& button, const std::string& indent,
                       std::size_t imageIndex,
                       // 图片（A11）复用这一份：数据同构（`ImageData` 派生自
                       // `ButtonData`），差别只有"类型名"与"末尾多一行
                       // runtimeAsset"。重写一遍逐字段烘法就等着两处走偏
                       // ——那正是 `ImageData` 选择继承的原因。
                       const char* dataType = "inking::ButtonData",
                       const std::string& tailLines = {}) {
    const inking::ButtonData& data = button.data;
    std::ostringstream out;

    out << indent << dataType << " data;\n";
    out << indent << "data.name = " << Quote(data.name) << ";\n";
    out << indent << "data.width = " << data.width << ";\n";
    out << indent << "data.height = " << data.height << ";\n";

    const auto appearance = [&](const std::string& field,
                                const inking::ButtonAppearance& value,
                                const std::vector<std::uint8_t>* imageBytes,
                                const std::string& imageArray) {
        out << indent << "data." << field << ".type = "
            << (value.type == inking::AppearanceType::Color
                    ? "inking::AppearanceType::Color"
                    : (value.type == inking::AppearanceType::Image
                           ? "inking::AppearanceType::Image"
                           : "inking::AppearanceType::Svg"))
            << ";\n";
        out << indent << "data." << field << ".color = " << Color(value.color)
            << ";\n";
        if (!value.source.empty()) {
            out << indent << "data." << field << ".source = "
                << Quote(value.source) << ";\n";
        }
        // 过渡时长：进这个状态时用。0（没配）就不写，生成物干净一点。
        if (value.transitionSeconds > 0.0f) {
            out << indent << "data." << field << ".transitionSeconds = "
                << Float(value.transitionSeconds) << ";\n";
        }
        // 缓动曲线：缺省的 linear 不写（写了也只是把同样的值抄一遍）。
        if (value.easing != inking::Easing::Linear) {
            out << indent << "data." << field << ".easing = inking::Easing::"
                << EasingIdentifier(value.easing) << ";\n";
        }
        // 布局通道（T-5）：这个状态自己的尺寸。0（没写）就不写那一行。
        if (value.width > 0.0f) {
            out << indent << "data." << field << ".width = "
                << Float(value.width) << ";\n";
        }
        if (value.height > 0.0f) {
            out << indent << "data." << field << ".height = "
                << Float(value.height) << ";\n";
        }
        // 图像字节：**只借一个视图**，真正的存储在文件作用域的静态数组里
        // （见 MakeSource 开头那段——它的生命周期是整个进程，比按钮长）。
        if (value.type == inking::AppearanceType::Image && imageBytes != nullptr
            && !imageBytes->empty()) {
            out << indent << "data." << field << ".imageBytes = " << imageArray
                << ";\n";
            out << indent << "data." << field << ".imageSize = sizeof("
                << imageArray << ");\n";
        }
        // 图片适配模式（B1）：缺省 fill 不写那一行。
        if (value.imageFit != inking::ImageFit::Fill) {
            out << indent << "data." << field << ".imageFit = inking::ImageFit::"
                << (value.imageFit == inking::ImageFit::Contain ? "Contain"
                                                             : "Cover")
                << ";\n";
        }
        // 变换：同样只在真配了的时候写。字段顺序就是 TransformSpec 的声明顺序
        // （translateX / translateY / rotate / scale）——聚合初始化靠这个顺序，
        // 写反了不会报错，只会静默地把 rotate 当成 scale。
        if (!value.transform.IsIdentity()) {
            out << indent << "data." << field << ".transform = inking::TransformSpec{"
                << Float(value.transform.translateX) << ", "
                << Float(value.transform.translateY) << ", "
                << Float(value.transform.rotate) << ", "
                << Float(value.transform.scale) << "};\n";
        }
    };

    appearance("normal", data.normal, &button.imageBytes[0],
               ImageArrayName(button, 0, imageIndex));
    if (!data.hoverInheritsNormal) {
        appearance("hover", data.hover, &button.imageBytes[1],
                   ImageArrayName(button, 1, imageIndex));
    }
    if (!data.onclickedInheritsNormal) {
        appearance("onclicked", data.onclicked, &button.imageBytes[2],
                   ImageArrayName(button, 2, imageIndex));
    }

    out << indent << "data.hoverInheritsNormal = "
        << (data.hoverInheritsNormal ? "true" : "false") << ";\n";
    out << indent << "data.onclickedInheritsNormal = "
        << (data.onclickedInheritsNormal ? "true" : "false") << ";\n";

    // 描边与阴影（B3）：与 shape 平级的两笔装饰。width = 0 / 颜色全透明
    // 就是没配，不写那两行（生成的代码短一点，也少一次无意义的赋值）。
    if (data.stroke.width > 0.0f) {
        out << indent << "data.stroke.width = " << Float(data.stroke.width)
            << ";\n";
        out << indent << "data.stroke.color = " << Color(data.stroke.color)
            << ";\n";
    }
    if (data.shadow.offsetX != 0.0f || data.shadow.offsetY != 0.0f
        || data.shadow.color != inking::ShadowSpec{}.color) {
        out << indent << "data.shadow.offsetX = " << Float(data.shadow.offsetX)
            << ";\n";
        out << indent << "data.shadow.offsetY = " << Float(data.shadow.offsetY)
            << ";\n";
        out << indent << "data.shadow.color = " << Color(data.shadow.color)
            << ";\n";
    }

    out << indent << "data.text.fontSize = " << Float(data.text.fontSize)
        << ";\n";
    out << indent << "data.text.leftSpace = " << Float(data.text.leftSpace)
        << ";\n";
    out << indent << "data.text.topSpace = " << Float(data.text.topSpace)
        << ";\n";
    // 对齐只在**不是默认值**时输出：老配置的产物一个字节都不变（diff 干净）。
    if (data.text.align != inking::TextAlignH::Left) {
        out << indent << "data.text.align = inking::TextAlignH::"
            << (data.text.align == inking::TextAlignH::Center ? "Center" : "Right")
            << ";\n";
    }
    if (data.text.alignV != inking::TextAlignV::Top) {
        out << indent << "data.text.alignV = inking::TextAlignV::"
            << (data.text.alignV == inking::TextAlignV::Middle ? "Middle"
                                                            : "Bottom")
            << ";\n";
    }
    if (!data.text.path.empty()) {
        out << indent << "data.text.path = " << Quote(data.text.path) << ";\n";
    }
    if (!data.label.empty()) {
        out << indent << "data.label = " << Quote(data.label) << ";\n";
    }

    out << indent << "data.selfAnchor = inking::AnchorPoint{" << Float(data.selfAnchor.x)
        << ", " << Float(data.selfAnchor.y) << "};\n";
    out << indent << "data.traceAnchor = inking::AnchorPoint{"
        << Float(data.traceAnchor.x) << ", " << Float(data.traceAnchor.y)
        << "};\n";
    out << indent << "data.offsetX = " << Float(data.offsetX) << ";\n";
    out << indent << "data.offsetY = " << Float(data.offsetY) << ";\n";
    out << indent << "data.zIndex = " << data.zIndex << ";\n";
    out << indent << "data.visible = " << (data.visible ? "true" : "false")
        << ";\n";

    out << ShapeAssignment(data.shape, indent, "data.shape",
                           PathArrayName(button, imageIndex));

    out << LayoutAssignments(data.layout, data.layoutItem, indent,
                             "data.layout", "data.layoutItem");

    if (!tailLines.empty()) {
        out << tailLines;
    }
    out << indent << "return data;\n";
    return out.str();
}

/**
 * 布局字段（A6）→ C++ 赋值语句。
 *
 * **只在非默认时输出**：绝大多数组件不带布局（`kind = None`、item 全空），
 * 每个生成类都摊开写一遍默认值只会让产物变胖、还淹没真正配了的东西。
 */
std::string LayoutAssignments(const inking::LayoutSpec& spec,
                              const inking::LayoutItem& item,
                              const std::string& indent,
                              const std::string& specTarget,
                              const std::string& itemTarget) {
    std::ostringstream out;

    if (spec.kind != inking::LayoutKind::None) {
        out << indent << specTarget << ".kind = inking::LayoutKind::"
            << (spec.kind == inking::LayoutKind::Grid ? "Grid" : "Stack")
            << ";\n";
        if (spec.axis == inking::LayoutAxis::Horizontal) {
            out << indent << specTarget << ".axis = inking::LayoutAxis::Horizontal;\n";
        }
        if (spec.gap != 0.0f) {
            out << indent << specTarget << ".gap = " << Float(spec.gap) << ";\n";
        }
        const auto spacing = [&](const char* field, float value) {
            if (value != 0.0f) {
                out << indent << specTarget << ".padding." << field << " = "
                    << Float(value) << ";\n";
            }
        };
        spacing("left", spec.padding.left);
        spacing("top", spec.padding.top);
        spacing("right", spec.padding.right);
        spacing("bottom", spec.padding.bottom);

        const auto justifyName = [](inking::LayoutJustify value) {
            switch (value) {
                case inking::LayoutJustify::Center: return "Center";
                case inking::LayoutJustify::End: return "End";
                case inking::LayoutJustify::SpaceBetween: return "SpaceBetween";
                case inking::LayoutJustify::SpaceAround: return "SpaceAround";
                case inking::LayoutJustify::SpaceEvenly: return "SpaceEvenly";
                case inking::LayoutJustify::Start:
                default: return "Start";
            }
        };
        const auto alignName = [](inking::LayoutAlign value) {
            switch (value) {
                case inking::LayoutAlign::Center: return "Center";
                case inking::LayoutAlign::End: return "End";
                case inking::LayoutAlign::Stretch: return "Stretch";
                case inking::LayoutAlign::Start:
                default: return "Start";
            }
        };
        if (spec.justify != inking::LayoutJustify::Start) {
            out << indent << specTarget << ".justify = inking::LayoutJustify::"
                << justifyName(spec.justify) << ";\n";
        }
        if (spec.align != inking::LayoutAlign::Stretch) {
            out << indent << specTarget << ".align = inking::LayoutAlign::"
                << alignName(spec.align) << ";\n";
        }
        if (spec.kind == inking::LayoutKind::Grid) {
            out << indent << specTarget << ".columns = " << spec.columns << ";\n";
            if (spec.rowGap != 0.0f) {
                out << indent << specTarget << ".rowGap = " << Float(spec.rowGap)
                    << ";\n";
            }
            if (spec.columnGap != 0.0f) {
                out << indent << specTarget << ".columnGap = "
                    << Float(spec.columnGap) << ";\n";
            }
        }
    }

    const bool itemUsed = item.margin.left != 0.0f || item.margin.top != 0.0f
                       || item.margin.right != 0.0f || item.margin.bottom != 0.0f
                       || item.fillMain || item.minWidth != 0.0f
                       || item.minHeight != 0.0f || item.maxWidth != 0.0f
                       || item.maxHeight != 0.0f || item.hasSelfAlign;
    if (itemUsed) {
        const auto itemSpacing = [&](const char* field, float value) {
            if (value != 0.0f) {
                out << indent << itemTarget << ".margin." << field << " = "
                    << Float(value) << ";\n";
            }
        };
        itemSpacing("left", item.margin.left);
        itemSpacing("top", item.margin.top);
        itemSpacing("right", item.margin.right);
        itemSpacing("bottom", item.margin.bottom);
        if (item.fillMain) {
            out << indent << itemTarget << ".fillMain = true;\n";
        }
        if (item.minWidth != 0.0f) {
            out << indent << itemTarget << ".minWidth = " << Float(item.minWidth)
                << ";\n";
        }
        if (item.minHeight != 0.0f) {
            out << indent << itemTarget << ".minHeight = " << Float(item.minHeight)
                << ";\n";
        }
        if (item.maxWidth != 0.0f) {
            out << indent << itemTarget << ".maxWidth = " << Float(item.maxWidth)
                << ";\n";
        }
        if (item.maxHeight != 0.0f) {
            out << indent << itemTarget << ".maxHeight = " << Float(item.maxHeight)
                << ";\n";
        }
        if (item.hasSelfAlign) {
            const char* name = "Start";
            switch (item.selfAlign) {
                case inking::LayoutAlign::Center: name = "Center"; break;
                case inking::LayoutAlign::End: name = "End"; break;
                case inking::LayoutAlign::Stretch: name = "Stretch"; break;
                case inking::LayoutAlign::Start:
                default: break;
            }
            out << indent << itemTarget << ".hasSelfAlign = true;\n";
            out << indent << itemTarget << ".selfAlign = inking::LayoutAlign::" << name
                << ";\n";
        }
    }

    return out.str();
}

/**
 * 一份面板配置 → 一个"造 PanelData"的表达式块。
 *
 * 与 `ButtonBody` 是同一套写法（逐字段赋值、只写真配了的字段）。面板少一截：
 * 没有三态、没有文字、没有图像字节；多一截：两个具名状态（closed / open）
 * 和 `startOpen`。
 */
std::string PanelBody(const PanelConfig& panel, const std::string& indent) {
    const inking::PanelData& data = panel.data;
    std::ostringstream out;

    out << indent << "inking::PanelData data;\n";
    out << indent << "data.name = " << Quote(data.name) << ";\n";
    out << indent << "data.width = " << data.width << ";\n";
    out << indent << "data.height = " << data.height << ";\n";

    const auto appearance = [&](const std::string& field,
                                const inking::PanelAppearance& value) {
        out << indent << "data." << field << ".color = " << Color(value.color)
            << ";\n";
        // 过渡时长 / 曲线：只写真配了的（0 与 linear 就是缺省值）。
        if (value.transitionSeconds > 0.0f) {
            out << indent << "data." << field << ".transitionSeconds = "
                << Float(value.transitionSeconds) << ";\n";
        }
        if (value.easing != inking::Easing::Linear) {
            out << indent << "data." << field << ".easing = inking::Easing::"
                << EasingIdentifier(value.easing) << ";\n";
        }
        if (!value.transform.IsIdentity()) {
            out << indent << "data." << field
                << ".transform = inking::TransformSpec{"
                << Float(value.transform.translateX) << ", "
                << Float(value.transform.translateY) << ", "
                << Float(value.transform.rotate) << ", "
                << Float(value.transform.scale) << "};\n";
        }
    };

    appearance("closed", data.closed);
    if (!data.openInheritsClosed) {
        appearance("open", data.open);
    }
    out << indent << "data.openInheritsClosed = "
        << (data.openInheritsClosed ? "true" : "false") << ";\n";

    if (data.startOpen) {
        out << indent << "data.startOpen = true;\n";
    }
    if (data.clipChildren) {
        // A5：裁剪只在显式声明时生成，默认那份 PanelData 就是 false。
        out << indent << "data.clipChildren = true;\n";
    }

    if (data.scroll.enabled) {
        // B12：滚动容器只在配了 `"scroll"` 段时生成（默认那份是关的）。
        // 颜色这里不写——语义色走 Theme 表，生成期解析成字面量（和别的段一样）。
        const inking::ScrollSpec& scroll = data.scroll;
        const auto axisName = [](inking::ScrollAxisMode mode) {
            switch (mode) {
            case inking::ScrollAxisMode::Always:
                return "Always";
            case inking::ScrollAxisMode::Never:
                return "Never";
            default:
                return "Auto";
            }
        };
        out << indent << "data.scroll.enabled = true;\n";
        out << indent << "data.scroll.vertical = "
            << (scroll.vertical ? "true" : "false") << ";\n";
        out << indent << "data.scroll.horizontal = "
            << (scroll.horizontal ? "true" : "false") << ";\n";
        out << indent << "data.scroll.wheelStep = " << Float(scroll.wheelStep)
            << ";\n";
        out << indent
            << "data.scroll.verticalBar = inking::ScrollAxisMode::"
            << axisName(scroll.verticalBar) << ";\n";
        out << indent
            << "data.scroll.horizontalBar = inking::ScrollAxisMode::"
            << axisName(scroll.horizontalBar) << ";\n";
        out << indent << "data.scroll.barWidth = " << Float(scroll.barWidth)
            << ";\n";
        out << indent << "data.scroll.barInset = " << Float(scroll.barInset)
            << ";\n";
        out << indent << "data.scroll.minThumbLength = "
            << Float(scroll.minThumbLength) << ";\n";
        out << indent << "data.scroll.barRadius = " << Float(scroll.barRadius)
            << ";\n";
    }

    out << indent << "data.selfAnchor = inking::AnchorPoint{" << Float(data.selfAnchor.x)
        << ", " << Float(data.selfAnchor.y) << "};\n";
    out << indent << "data.traceAnchor = inking::AnchorPoint{"
        << Float(data.traceAnchor.x) << ", " << Float(data.traceAnchor.y)
        << "};\n";
    out << indent << "data.offsetX = " << Float(data.offsetX) << ";\n";
    out << indent << "data.offsetY = " << Float(data.offsetY) << ";\n";
    out << indent << "data.zIndex = " << data.zIndex << ";\n";
    out << indent << "data.visible = " << (data.visible ? "true" : "false")
        << ";\n";

    out << ShapeAssignment(data.shape, indent, "data.shape");
    out << LayoutAssignments(data.layout, data.layoutItem, indent,
                             "data.layout", "data.layoutItem");

    out << indent << "return data;\n";
    return out.str();
}

/**
 * 一份标签配置 → 一个"造 LabelData"的表达式块（A7 的生成器段）。
 *
 * 照 `PanelBody` 抄：逐字段赋值、只写真配了的字段。标签的三块是
 * **文字内容**（`text`）、**排版样式**（`style` 那几项 + 换行 / 省略）、
 * **组件几何**（尺寸 / 锚点 / 布局）。
 *
 * 高度那一项有意只在 > 0 时写：0 就是"由内容撑高"，写出来只是把默认值抄一遍。
 */
std::string LabelBody(const LabelConfig& label, const std::string& indent) {
    const inking::LabelData& data = label.data;
    std::ostringstream out;

    out << indent << "inking::LabelData data;\n";
    out << indent << "data.name = " << Quote(data.name) << ";\n";
    out << indent << "data.width = " << data.width << ";\n";
    if (data.height > 0) {
        out << indent << "data.height = " << data.height << ";\n";
    }

    // 文字内容：字面量或**占位符**（运行期交给 ResolveText 查语言表）。
    if (!data.text.empty()) {
        out << indent << "data.text = " << Quote(data.text) << ";\n";
    }
    // 文字色：缺省是白，只在不同时写一行。
    if (data.color != inking::LabelData{}.color) {
        out << indent << "data.color = " << Color(data.color) << ";\n";
    }

    // 排版样式：与 TextStyle 的默认值不同的才写（默认值不占产物篇幅）。
    const inking::TextStyle defaultStyle{};
    if (data.style.fontSize != defaultStyle.fontSize) {
        out << indent << "data.style.fontSize = " << Float(data.style.fontSize)
            << ";\n";
    }
    if (data.style.lineSpacing != defaultStyle.lineSpacing) {
        out << indent << "data.style.lineSpacing = "
            << Float(data.style.lineSpacing) << ";\n";
    }
    if (!data.style.fontPath.empty()) {
        out << indent << "data.style.fontPath = " << Quote(data.style.fontPath)
            << ";\n";
    }
    if (data.style.align != defaultStyle.align) {
        out << indent << "data.style.align = inking::TextAlignH::"
            << TextAlignHIdentifier(data.style.align) << ";\n";
    }
    if (data.style.alignV != defaultStyle.alignV) {
        out << indent << "data.style.alignV = inking::TextAlignV::"
            << TextAlignVIdentifier(data.style.alignV) << ";\n";
    }
    // 文字与文本框四边的留白：四边各写非 0 的那些。
    const auto padding = [&](const char* field, float value) {
        if (value != 0.0f) {
            out << indent << "data.style.padding." << field << " = "
                << Float(value) << ";\n";
        }
    };
    padding("left", data.style.padding.left);
    padding("top", data.style.padding.top);
    padding("right", data.style.padding.right);
    padding("bottom", data.style.padding.bottom);

    // 换行 / 省略 / 行数上限：缺省（word / clip / 不限）不写。
    if (data.wrap != inking::TextWrap::Word) {
        out << indent << "data.wrap = inking::TextWrap::"
            << TextWrapIdentifier(data.wrap) << ";\n";
    }
    if (data.overflow != inking::TextOverflow::Clip) {
        out << indent << "data.overflow = inking::TextOverflow::"
            << TextOverflowIdentifier(data.overflow) << ";\n";
    }
    if (data.maxLines != 0) {
        out << indent << "data.maxLines = " << data.maxLines << ";\n";
    }

    out << indent << "data.selfAnchor = inking::AnchorPoint{" << Float(data.selfAnchor.x)
        << ", " << Float(data.selfAnchor.y) << "};\n";
    out << indent << "data.traceAnchor = inking::AnchorPoint{"
        << Float(data.traceAnchor.x) << ", " << Float(data.traceAnchor.y)
        << "};\n";
    out << indent << "data.offsetX = " << Float(data.offsetX) << ";\n";
    out << indent << "data.offsetY = " << Float(data.offsetY) << ";\n";
    out << indent << "data.zIndex = " << data.zIndex << ";\n";
    out << indent << "data.visible = " << (data.visible ? "true" : "false")
        << ";\n";

    out << LayoutAssignments(data.layout, data.layoutItem, indent,
                             "data.layout", "data.layoutItem");

    out << indent << "return data;\n";
    return out.str();
}

/**
 * 一份语言表配置 → 一个"造 TextTable"的表达式块（A7 的生成器段）。
 *
 * 与别的组件不同，它不是"一个类 + 一份数据"：语言表是**全局表**，产物里就是
 * 一组字面量 + 一个安装函数（见 `MakeSource`）。这里只负责把表本体写出来。
 *
 * `languages` 的顺序就是 `translations` 的顺序（加载期已经对过账），所以这里
 * 不需要再做任何映射——错位在生成期就红了。
 */
std::string TextTableBody(const TextConfig& text, const std::string& indent) {
    const inking::TextTable& table = text.table;
    std::ostringstream out;

    out << indent << "inking::TextTable table;\n";
    out << indent << "table.languages = {";
    for (std::size_t i = 0; i < table.languages.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << Quote(table.languages[i]);
    }
    out << "};\n";

    for (const inking::TextEntry& entry : table.entries) {
        out << indent << "{\n";
        out << indent << "    inking::TextEntry entry;\n";
        out << indent << "    entry.key = " << Quote(entry.key) << ";\n";
        out << indent << "    entry.translations = {";
        for (std::size_t i = 0; i < entry.translations.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << Quote(entry.translations[i]);
        }
        out << "};\n";
        out << indent << "    table.entries.push_back(entry);\n";
        out << indent << "}\n";
    }

    out << indent << "return table;\n";
    return out.str();
}

std::string MakeHeader(const LoadResult& loaded, const EmitOptions& options) {
    const std::vector<ButtonConfig>& buttons = loaded.buttons;
    const std::vector<PanelConfig>& panels = loaded.panels;
    const std::vector<LabelConfig>& labels = loaded.labels;
    const std::vector<ImageConfig>& images = loaded.images;
    const std::vector<TextConfig>& textTables = loaded.textTables;
    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    std::ostringstream out;

    out << "// 本文件由 inkgen 生成，不要手改。\n";
    out << "// 来源：CXXCSS/<组件类型>/*.json（本次 " << buttons.size()
        << " 个按钮 + " << panels.size() << " 个面板 + " << labels.size()
        << " 个标签 + " << textTables.size() << " 张语言表）\n";
    if (!loaded.themes.empty()) {
        // 主题只留一条来源注释：它是**生成器的输入**——`$token` 在生成期就换成了
        // 具体值，所以产物里没有主题类（换主题要重新生成，见 CXXCSS.md §3.9）。
        out << "// 主题：" << loaded.themes.front().name << "（"
            << loaded.themes.front().filePath
            << "）—— 配置里的 $token 已在生成期换成具体值\n";
    }
    out << "// 改配置请改 json；改生成规则请改 tools/inkgen/Emit.cpp。\n";
    out << "#pragma once\n\n";
    if (!buttons.empty()) {
        out << "#include <button/ButtonData.h>\n";
        out << "#include <button/InkingDynamicButton.h>\n";
        out << "#include <button/InkingStaticButton.h>\n";
    }
    if (!panels.empty()) {
        out << "#include <panel/InkingStaticPanel.h>\n";
        out << "#include <panel/PanelData.h>\n";
    }
    if (!labels.empty()) {
        out << "#include <text/InkingStaticLabel.h>\n";
        out << "#include <text/LabelData.h>\n";
    }
    if (!images.empty()) {
        out << "#include <image/ImageData.h>\n";
        out << "#include <image/InkingStaticImage.h>\n";
    }
    if (!textTables.empty()) {
        out << "#include <text/InkingTextTable.h>\n";
    }
    out << "\n#include <cstddef>\n\n";
    out << OpenNamespaces(parts);
    if (!buttons.empty()) {
        out << "\n// ---------------------------------------------------------------------------\n";
        out << "// 每个按钮一个独立的类：数据直接烘在类里（不是运行期查表）\n";
        out << "//\n";
        out << "// 为什么不是\"名字 → 运行期查表\"：那样每个字段都要在运行期从一个共享表里取，\n";
        out << "// 而且\"名字对但没登记\"这条路径只能靠静默退回默认值——那是个会藏 bug 的设计。\n";
        out << "// 生成类之后：拼错名字编译期就红，配置\"取不到\"这件事在类型上不可能发生。\n";
        out << "//\n";
        out << "// 基类由 json 的 \"dynamic\" 决定（静态 / 动态是二选一）：\n";
        out << "//   不写或 false → inking::InkingStaticButton（几何构造即定型，进命中表）\n";
        out << "//   true         → inking::InkingDynamicButton（几何可运行期改，自己拉指针输入）\n";
        out << "// ---------------------------------------------------------------------------\n";
    }

    for (const ButtonConfig& button : buttons) {
        if (button.identifier.empty()) {
            out << "\n// 名字 \"" << button.name
                << "\" 不是合法标识符，没法生成类名与常量；\n"
                   "// 它会出现在 RegisterAllButtons() 里，只能按字符串名字使用。\n";
            continue;
        }

        const char* const baseClass = button.dynamic
                                          ? "inking::InkingDynamicButton"
                                          : "inking::InkingStaticButton";
        out << "\n/// " << button.name << "（来自 " << button.filePath
            << (button.dynamic ? "，动态按钮" : "，静态按钮") << "）\n";
        out << "class " << button.identifier << " : public " << baseClass
            << " {\n";
        out << "public:\n";
        out << "    /// 查找键（= json 的 name 字段）。\n";
        out << "    static constexpr const char* kName = " << Quote(button.name)
            << ";\n\n";
        out << "    explicit " << button.identifier
            << "(inking::InkingWidget* parent)\n";
        out << "        : " << baseClass << "(parent, Data()) {}\n\n";
        out << "    /**\n";
        out << "     * 这份配置。值全是字面量，烘在构建产物里——\n";
        out << "     * 没有查表、没有\"取不到就退回默认外观\"这条路。\n";
        out << "     *\n";
        out << "     * 函数内静态量：首次使用时才初始化，避开静态初始化 / 析构顺序问题\n";
        out << "     * （docs/AGENTS.md §6 第 12 条）。内容全是常量，多线程读也安全。\n";
        out << "     */\n";
        out << "    static const inking::ButtonData& Data();\n\n";
        out << "    /// 把这份配置登记进 inking::ButtonLibrary（给\"按名字查\"的用法）。\n";
        out << "    static void Register();\n";
        out << "};\n";
    }

    if (!panels.empty()) {
        out << "\n// ---------------------------------------------------------------------------\n";
        out << "// 面板（Panel）：容器组件，只有**静态档**\n";
        out << "//\n";
        out << "// 它与按钮的差别不是「少几行」，而是「另一类」：面板是**容器**——开合时\n";
        out << "// 它的变换作用于整棵子树（子节点绘制与命中一起动），子孙不进命中表。\n";
        out << "// 所以它的类只需要把 `PanelData` 交上去，行为全在 `inking::InkingStaticPanel` 里。\n";
        out << "//\n";
        out << "// 没有 `Register()`：面板**没有**按名字查的库（按钮有 `ButtonLibrary`，\n";
        out << "// 那是给「运行期才知道名字」的用法准备的）。面板只有数据构造这一条路，\n";
        out << "// 所以它的 name 必须是合法标识符——生成期会拦下不合法的。\n";
        out << "// ---------------------------------------------------------------------------\n";
    }

    for (const PanelConfig& panel : panels) {
        out << "\n/// " << panel.name << "（来自 " << panel.filePath
            << "，静态面板）\n";
        out << "class " << panel.identifier
            << " : public inking::InkingStaticPanel {\n";
        out << "public:\n";
        out << "    /// 查找键（= json 的 name 字段）。\n";
        out << "    static constexpr const char* kName = " << Quote(panel.name)
            << ";\n\n";
        out << "    explicit " << panel.identifier
            << "(inking::InkingWidget* parent)\n";
        out << "        : inking::InkingStaticPanel(parent, Data()) {}\n\n";
        out << "    /**\n";
        out << "     * 这份配置。值全是字面量，烘在构建产物里——\n";
        out << "     * 没有查表、没有\"取不到就退回默认外观\"这条路。\n";
        out << "     *\n";
        out << "     * 函数内静态量：首次使用时才初始化，避开静态初始化 / 析构顺序问题\n";
        out << "     * （docs/AGENTS.md §6 第 12 条）。内容全是常量，多线程读也安全。\n";
        out << "     */\n";
        out << "    static const inking::PanelData& Data();\n";
        out << "};\n";
    }

    if (!labels.empty()) {
        out << "\n// ---------------------------------------------------------------------------\n";
        out << "// 标签（Label）：一段只读文字，按内容撑高（A7）\n";
        out << "//\n";
        out << "// 它也是一个组件（一个名字一个类），但**没有按名字查的库**——和面板一样，\n";
        out << "// 名字必须是合法标识符：生成不出类就等于没有任何用法。\n";
        out << "//\n";
        out << "// 行为全在 `inking::InkingStaticLabel` 里：默认**不吃点击**（标签常压在按钮 /\n";
        out << "// 图片上）、`MeasureLayout` 报内容尺寸（A6 那条「label 按文字撑高」）、\n";
        out << "// `text.content` 是占位符时会走 `ResolveText` 查语言表。\n";
        out << "// ---------------------------------------------------------------------------\n";
    }

    for (const LabelConfig& label : labels) {
        out << "\n/// " << label.name << "（来自 " << label.filePath
            << "，静态标签）\n";
        out << "class " << label.identifier
            << " : public inking::InkingStaticLabel {\n";
        out << "public:\n";
        out << "    /// 查找键（= json 的 name 字段）。\n";
        out << "    static constexpr const char* kName = " << Quote(label.name)
            << ";\n\n";
        out << "    explicit " << label.identifier
            << "(inking::InkingWidget* parent)\n";
        out << "        : inking::InkingStaticLabel(parent, Data()) {}\n\n";
        out << "    /**\n";
        out << "     * 这份配置。值全是字面量，烘在构建产物里——\n";
        out << "     * 没有查表、没有\"取不到就退回默认外观\"这条路。\n";
        out << "     *\n";
        out << "     * 函数内静态量：首次使用时才初始化，避开静态初始化 / 析构顺序问题\n";
        out << "     * （docs/AGENTS.md §6 第 12 条）。内容全是常量，多线程读也安全。\n";
        out << "     */\n";
        out << "    static const inking::LabelData& Data();\n";
        out << "};\n";
    }

    for (const ImageConfig& image : images) {
        out << "\n/// " << image.name << "（来自 " << image.filePath
            << "，静态图片）\n";
        out << "class " << image.identifier
            << " : public inking::InkingStaticImage {\n";
        out << "public:\n";
        out << "    /// 查找键（= json 的 name 字段）。\n";
        out << "    static constexpr const char* kName = " << Quote(image.name)
            << ";\n\n";
        out << "    explicit " << image.identifier
            << "(inking::InkingWidget* parent)\n";
        out << "        : inking::InkingStaticImage(parent, Data()) {}\n\n";
        out << "    /**\n";
        out << "     * 这份配置。值全是字面量（或一个 assets 相对路径），烘在构建产物里。\n";
        out << "     *\n";
        out << "     * 配了 `\"runtime\": true` 的那张图，产物里**只有路径**——字节由\n";
        out << "     * 运行期的 `inking::AssetLoad` 从资源根读（换图不用重编，exe 也不随\n";
        out << "     * 图片数量膨胀）。\n";
        out << "     */\n";
        out << "    static const inking::ImageData& Data();\n";
        out << "};\n";
    }

    if (!textTables.empty()) {
        out << "\n// ---------------------------------------------------------------------------\n";
        out << "// 语言表（Text）：占位符 → 各语言的串（A7）\n";
        out << "//\n";
        out << "// 它是四种类型里唯一**不是组件**的那个：没有类、没有实例，只有一份全局\n";
        out << "// 数据 + 一个安装函数；启动期自动装一次（见源文件末尾那个启动对象）。\n";
        out << "// 所以业务代码通常一个字都不用写，直接用 `inking::cxxcss` 里的标签就行。\n";
        out << "//\n";
        out << "// 换语言：`inking::SetTextLanguage(\"en\")`。**不用通知任何组件**——语言版本号\n";
        out << "// 一变，标签的排版缓存自然失效，下一帧自己重查、重排。\n";
        out << "// ---------------------------------------------------------------------------\n";
        out << "\n/// 这张语言表有几门语言 / 几条占位符（自检与工具用）。\n";
        out << "inline constexpr std::size_t kTextLanguageCount = "
            << textTables.front().table.languages.size() << ";\n";
        out << "inline constexpr std::size_t kTextEntryCount = "
            << textTables.front().table.entries.size() << ";\n";
        out << "\n/// 这张语言表的**本体**（进程内静态量：首次用到才构造）。\n";
        out << "const inking::TextTable& GeneratedTextTable();\n";
        out << "\n/// 把它装进运行期（**整表替换**；缺省语言取自 json 的 default）。\n";
        out << "/// 启动期已经自动调过一次；手动再调就是「重新装一遍」。\n";
        out << "void InstallGeneratedTextTable();\n";
    }

    out << "\n/// 本次生成里有多少个按钮。\n";
    out << "inline constexpr std::size_t kButtonCount = " << buttons.size()
        << ";\n";
    out << "\n/// 本次生成里有多少个面板。\n";
    out << "inline constexpr std::size_t kPanelCount = " << panels.size()
        << ";\n";
    out << "\n/// 本次生成里有多少个标签。\n";
    out << "inline constexpr std::size_t kLabelCount = " << labels.size()
        << ";\n";

    out << "\n/// 把本次生成的全部**按钮**配置登记进 inking::ButtonLibrary。\n";
    out << "///\n";
    out << "/// **不是**按钮能用的前提——每个按钮类自己带着数据。这个入口是给\n";
    out << "/// 「按名字查配置」那类用法准备的（自检、工具、将来运行期换皮肤）。\n";
    out << "/// 幂等：重复调用会先把上一次登记的这批撤下再重新登记。\n";
    out << "///\n";
    out << "/// 面板与标签不在这里：它们没有按名字查的库（数据烘在各自的类里）。\n";
    out << "void RegisterAllButtons();\n";

    out << CloseNamespaces(parts);
    return out.str();
}

std::string MakeSource(const LoadResult& loaded, const EmitOptions& options) {
    const std::vector<ButtonConfig>& buttons = loaded.buttons;
    const std::vector<PanelConfig>& panels = loaded.panels;
    const std::vector<LabelConfig>& labels = loaded.labels;
    const std::vector<ImageConfig>& images = loaded.images;
    const std::vector<TextConfig>& textTables = loaded.textTables;
    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    const std::string registerAll = Qualified(options.nameSpace,
                                              "RegisterAllButtons");
    std::ostringstream out;

    out << "// 本文件由 inkgen 生成，不要手改。\n";
    out << "#include <" << options.headerName << ">\n\n";
    if (!buttons.empty()) {
        out << "#include <button/ButtonLibrary.h>\n\n";
    }

    // ---- 每个组件一个 Build()（内部链接，不导出符号） ----
    if (!buttons.empty() || !panels.empty() || !labels.empty()
        || !images.empty() || !textTables.empty()) {
        out << "namespace {\n\n";
        // 图像字节（T-4）：生成期从文件读进来的，烘成**文件作用域的静态数组**。
        // 生命周期是整个进程——比任何按钮都长，所以 ButtonData 里只借一个视图。
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const ButtonConfig& button = buttons[i];
            for (int state = 0; state < 3; ++state) {
                const std::vector<std::uint8_t>& bytes =
                    button.imageBytes[state];
                if (bytes.empty()) {
                    continue;
                }
                out << "/// " << button.name << " 的图像字节（"
                    << bytes.size() << " 字节）\n";
                out << "const unsigned char "
                    << ImageArrayName(button, state, i) << "[] = {\n";
                for (std::size_t at = 0; at < bytes.size(); ++at) {
                    if (at % 16 == 0) {
                        out << "    ";
                    }
                    out << "0x" << std::hex << std::setw(2)
                        << std::setfill('0')
                        << static_cast<unsigned>(bytes[at]) << std::dec;
                    if (at + 1 < bytes.size()) {
                        out << ", ";
                    }
                    if (at % 16 == 15 || at + 1 == bytes.size()) {
                        out << "\n";
                    }
                }
                out << "};\n\n";
            }

            // 形状的顶点表（B2）：和图像字节同一个道理——`ShapeSpec` 里只有视图，
            // 本体烘成文件作用域的静态数组（生命周期是整个进程）。
            if (button.data.shape.kind == inking::ShapeKind::PolygonPath
                && !button.pathPoints.empty()) {
                out << "/// " << button.name << " 的形状顶点表（"
                    << (button.pathPoints.size() / 2) << " 个点，设计坐标）\n";
                out << "const float " << PathArrayName(button, i) << "[] = {\n";
                for (std::size_t at = 0; at < button.pathPoints.size(); ++at) {
                    if (at % 8 == 0) {
                        out << "    ";
                    }
                    out << Float(button.pathPoints[at]);
                    if (at + 1 < button.pathPoints.size()) {
                        out << ", ";
                    }
                    if (at % 8 == 7 || at + 1 == button.pathPoints.size()) {
                        out << "\n";
                    }
                }
                out << "};\n\n";
            }
        }
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const ButtonConfig& button = buttons[i];
            out << "/// " << button.name << "（来自 " << button.filePath
                << "）\n";
            out << "inking::ButtonData Build" << i << "() {\n";
            out << ButtonBody(button, "    ", i);
            out << "}\n\n";
        }

        // 图片（A11）：`runtime` 那条路**一个字节都不烘**（产物里只有路径）——
        // 这就是"别把所有图烘进 exe"落到产物上的样子。
        for (std::size_t i = 0; i < images.size(); ++i) {
            const ImageConfig& image = images[i];
            const std::vector<std::uint8_t>& bytes
                = image.button.imageBytes[0];
            if (bytes.empty()) {
                continue;
            }
            out << "/// " << image.name << " 的图像字节（" << bytes.size()
                << " 字节）\n";
            out << "const unsigned char "
                << ImageArrayName(image.button, 0, i + buttons.size())
                << "[] = {\n";
            for (std::size_t at = 0; at < bytes.size(); ++at) {
                if (at % 16 == 0) {
                    out << "    ";
                }
                out << "0x" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(bytes[at]) << std::dec;
                if (at + 1 < bytes.size()) {
                    out << ", ";
                }
                if (at % 16 == 15 || at + 1 == bytes.size()) {
                    out << "\n";
                }
            }
            out << "};\n\n";
        }
        for (std::size_t i = 0; i < images.size(); ++i) {
            const ImageConfig& image = images[i];
            out << "/// " << image.name << "（来自 " << image.filePath
                << "）\n";
            out << "inking::ImageData BuildImage" << i << "() {\n";
            // 逐字复用按钮那套烘法（`ImageData` 派生自 `ButtonData`）：只把
            // 类型名换掉、末尾补一行 `runtimeAsset`。数组下标接在按钮后面，
            // 免得两边的 `kXxxImage0` 撞名。
            out << ButtonBody(image.button, "    ", i + buttons.size(),
                              "inking::ImageData",
                              std::string("    data.runtimeAsset = ")
                                  + (image.runtimeAsset ? "true" : "false")
                                  + ";\n");
            out << "}\n\n";
        }
        for (std::size_t i = 0; i < panels.size(); ++i) {
            const PanelConfig& panel = panels[i];
            out << "/// " << panel.name << "（来自 " << panel.filePath
                << "）\n";
            out << "inking::PanelData BuildPanel" << i << "() {\n";
            out << PanelBody(panel, "    ");
            out << "}\n\n";
        }
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const LabelConfig& label = labels[i];
            out << "/// " << label.name << "（来自 " << label.filePath
                << "）\n";
            out << "inking::LabelData BuildLabel" << i << "() {\n";
            out << LabelBody(label, "    ");
            out << "}\n\n";
        }
        for (std::size_t i = 0; i < textTables.size(); ++i) {
            const TextConfig& text = textTables[i];
            out << "/// 语言表 " << text.name << "（来自 " << text.filePath
                << "）\n";
            out << "inking::TextTable BuildTextTable" << i << "() {\n";
            out << TextTableBody(text, "    ");
            out << "}\n\n";
        }
        out << "}  // namespace\n\n";
    }

    out << OpenNamespaces(parts);

    // 每个按钮类：Data() 与 Register() 的定义。
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const ButtonConfig& button = buttons[i];
        if (button.identifier.empty()) {
            continue;
        }
        out << "\nconst inking::ButtonData& " << button.identifier
            << "::Data() {\n";
        out << "    static const inking::ButtonData data = Build" << i << "();\n";
        out << "    return data;\n";
        out << "}\n\n";
        out << "void " << button.identifier << "::Register() {\n";
        out << "    inking::ButtonLibrary::Register(Data());\n";
        out << "}\n";
    }

    // 每个面板类：只有 Data()——面板没有按名字查的库（见头文件里那段）。
    for (std::size_t i = 0; i < panels.size(); ++i) {
        const PanelConfig& panel = panels[i];
        out << "\nconst inking::PanelData& " << panel.identifier
            << "::Data() {\n";
        out << "    static const inking::PanelData data = BuildPanel" << i
            << "();\n";
        out << "    return data;\n";
        out << "}\n";
    }

    // 每个标签类：同样只有 Data()（标签也没有按名字查的库）。
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const LabelConfig& label = labels[i];
        out << "\nconst inking::LabelData& " << label.identifier
            << "::Data() {\n";
        out << "    static const inking::LabelData data = BuildLabel" << i
            << "();\n";
        out << "    return data;\n";
        out << "}\n";
    }

    // 每个图片类：同样只有 Data()（图片也没有按名字查的库）。
    for (std::size_t i = 0; i < images.size(); ++i) {
        const ImageConfig& image = images[i];
        out << "\nconst inking::ImageData& " << image.identifier
            << "::Data() {\n";
        out << "    static const inking::ImageData data = BuildImage" << i
            << "();\n";
        out << "    return data;\n";
        out << "}\n";
    }

    // 语言表：本体 + 安装。它不是组件，所以只有这两个函数。
    if (!textTables.empty()) {
        // 缺省语言：json 的 `default`；没写就用表里第一门（与 `InstallTextTable`
        // 不传语言时的行为一致）。
        const std::string defaultLanguage
            = !textTables.front().defaultLanguage.empty()
                  ? textTables.front().defaultLanguage
                  : (textTables.front().table.languages.empty()
                         ? std::string()
                         : textTables.front().table.languages.front());

        out << "\nconst inking::TextTable& GeneratedTextTable() {\n";
        out << "    // 函数内静态量：首次用到才构造（AGENTS.md §6 第 12 条）。\n";
        out << "    static const inking::TextTable table = BuildTextTable0();\n";
        out << "    return table;\n";
        out << "}\n\n";
        out << "void InstallGeneratedTextTable() {\n";
        out << "    // 整表替换：装第二次就是重新装一遍（不该有别的地方再装表）。\n";
        out << "    inking::InstallTextTable(BuildTextTable0(), "
            << Quote(defaultLanguage) << ");\n";
        out << "}\n";
    }

    out << "\nvoid RegisterAllButtons() {\n";
    if (buttons.empty()) {
        out << "    // 本次生成里没有按钮配置，这个入口是空的。\n";
        out << "    // （面板与标签不走这里：它们没有按名字查的库，数据烘在各自的类里；\n";
        out << "    //  语言表也不是组件——它在 InstallGeneratedTextTable() 那条路上。）\n";
    } else {
        out << "    // 先把上一批（同一个生成集合）撤下，再重新登记：这样重复调用、\n";
        out << "    // 运行期重建都不报 NameTaken，也不会碰到别的库内容。\n";
        out << "    inking::ButtonLibrary::BeginRegistrationBatch();\n\n";
        out << "    // 注意：**按钮能用不依赖这一步**。每个按钮类自己带着数据；\n";
        out << "    // 这里只是把同一份配置也放进按名字查的表里。\n";

        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const ButtonConfig& button = buttons[i];
            if (button.identifier.empty()) {
                out << "    {\n";
                out << "        // 名字不是合法标识符，没有对应的类，只能在这里登记。\n";
                out << "        inking::ButtonLibrary::Register(Build" << i
                    << "());\n";
                out << "    }\n";
                continue;
            }
            out << "    " << button.identifier << "::Register();\n";
        }

        out << "\n    inking::ButtonLibrary::EndRegistrationBatch();\n";
    }
    out << "}\n";

    out << CloseNamespaces(parts);

    if (buttons.empty() && textTables.empty()) {
        // 既没有"按名字查的表"要预热，也没有语言表要装，收工。
        return out.str();
    }

    out << "\nnamespace {\n\n";
    out << "/**\n";
    out << " * 启动期把生成出来的东西接到运行期上：按钮配置登记进按名字查的表、\n";
    out << " * 语言表装进 `inking::InstallTextTable`。\n";
    out << " *\n";
    out << " * 依赖静态初始化顺序的写法在这里是**安全**的：那两处用的都是函数内\n";
    out << " * 静态量，谁先碰它谁把它拉起来，所以这个对象的构造一定发生在库可用之后。\n";
    out << " */\n";
    out << "struct StartupRegistration {\n";
    out << "    StartupRegistration() {\n";
    if (!buttons.empty()) {
        out << "        " << registerAll << "();\n";
    }
    if (!textTables.empty()) {
        out << "        "
            << Qualified(options.nameSpace, "InstallGeneratedTextTable")
            << "();\n";
    }
    out << "    }\n";
    out << "} gStartupRegistration;\n\n";
    out << "}  // namespace\n";
    return out.str();
}

bool WriteFile(const std::string& path, const std::string& text,
               std::string& error) {
    // ---------------------------------------------------------------------
    // 产物统一带 UTF-8 BOM。
    //
    // 为什么：生成的头/源里全是中文注释，而**无 BOM 的 UTF-8** 会被
    // Visual Studio / 记事本按系统 ANSI（中文 Windows 上是 GBK）打开 ——
    // 中文全变乱码，用户第一眼就以为生成器坏了。GCC 与 MSVC 都接受 BOM，
    // 所以带上没有编译代价。
    //
    // BOM 参与下面的"内容没变就不写"比较，否则每次构建都会因为
    // "旧文件无 BOM / 新内容有 BOM"而重写一次、连带重编下游。
    // ---------------------------------------------------------------------
    const std::string bom = "\xEF\xBB\xBF";
    const std::string payload = bom + text;

    // 只在内容真的变了时才写：产物时间戳不动，下游就不会被无谓地重新编译。
    {
        std::ifstream existing(path, std::ios::binary);
        if (existing) {
            std::ostringstream buffer;
            buffer << existing.rdbuf();
            if (buffer.str() == payload) {
                return true;
            }
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "写不了文件：" + path;
        return false;
    }
    out << payload;
    if (!out) {
        error = "写入失败：" + path;
        return false;
    }
    return true;
}

}  // namespace

EmitResult Emit(const LoadResult& loaded, const EmitOptions& options) {
    EmitResult result;

    if (options.outDir.empty()) {
        result.error = "没有指定 --out-dir";
        return result;
    }

    // 有错的结果不该走到生成这一步（调用方保证 `LoadConfigs` 是 Ok 的）。
    // 检查一次而不是"默默生成一半"：半份产物比没有产物更难查。
    if (!loaded.Ok()) {
        result.error = "这次加载里有 " + std::to_string(loaded.errors.size())
                     + " 条错误，不能生成（先把配置改对）";
        return result;
    }

    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    if (parts.empty()) {
        result.error = "命名空间为空（--namespace 形如 inking::cxxcss）";
        return result;
    }
    for (const std::string& part : parts) {
        if (!IsCxxIdentifier(part)) {
            result.error = "命名空间段 \"" + part + "\" 不是合法标识符";
            return result;
        }
    }
    // 头文件名：相对路径，默认 `inkgen/button_service.h`。
    // 目录那几段会出现在生成源文件的 `#include` 里，也是 CMake 那边加进
    // include 路径的东西——这么设计是为了让产物永远写成
    // `#include <inkgen/xxx/button_service.h>`，和别的头文件放在一起也不撞名。
    // 只拦两件真会出事的事：绝对路径、以及".."这种会写到源码树里去的路径。
    const std::filesystem::path headerRel(options.headerName);
    if (headerRel.is_absolute()) {
        result.error = "头文件名 \"" + options.headerName
                     + "\" 不合法（要相对路径，产物必须落在 build 目录里）";
        return result;
    }
    for (const auto& part : headerRel) {
        if (part == "..") {
            result.error = "头文件名 \"" + options.headerName
                         + "\" 不合法（不允许 ..，产物必须落在 build 目录里）";
            return result;
        }
    }

    std::string stem = headerRel.filename().string();
    for (const char* suffix : {".hpp", ".h"}) {
        const std::size_t length = std::char_traits<char>::length(suffix);
        if (stem.size() > length
            && stem.compare(stem.size() - length, length, suffix) == 0) {
            stem.erase(stem.size() - length);
            break;
        }
    }
    if (stem.empty() || !IsCxxIdentifier(stem)) {
        result.error = "头文件名 \"" + options.headerName
                     + "\" 不合法（主体要是合法标识符，例如 button_service.h）";
        return result;
    }

    std::error_code code;
    // 产物目录 = --out-dir + HEADER 的目录部分。--out-dir 是"include 根"，
    // 而 HEADER 可能带子目录（`inkgen/buttons_demo/button_service.h`）。
    // 生成源文件里 include 的是 HEADER 原样的相对路径，所以两者必须对齐。
    const std::filesystem::path dir =
        headerRel.has_parent_path()
            ? std::filesystem::path(options.outDir) / headerRel.parent_path()
            : std::filesystem::path(options.outDir);
    std::filesystem::create_directories(dir, code);
    if (code) {
        result.error = "建不了输出目录 " + dir.string() + "：" + code.message();
        return result;
    }

    result.headerPath = (dir / headerRel.filename()).string();
    // 源文件名跟着头文件主体走（`button_service.h` → `button_service_register.cpp`）。
    // 固定叫 `button_register.cpp` 的话，产物里混了面板之后名不副实；
    // 而且同一个产物目录里想放两套生成结果时会撞名。
    result.sourcePath = (dir / (stem + "_register.cpp")).string();

    if (!WriteFile(result.headerPath, MakeHeader(loaded, options),
                   result.error)) {
        return result;
    }
    if (!WriteFile(result.sourcePath, MakeSource(loaded, options),
                   result.error)) {
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace inkgen
