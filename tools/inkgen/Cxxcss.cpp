#include "Cxxcss.h"
#include "Fields.h"
#include "PathUtil.h"

// 设计尺寸的**上限**只有一份定义：它住在公开头里（运行期也用它夹参数），
// 生成器这边直接引用，免得两个数字各自演化（`AGENTS.md` §6 那条
// "同一件事两份定义迟早分叉"）。
#include <config/window_config.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace inkgen {

namespace {

// 白名单集中在 `Fields.h`（0.2.0 T-08 的单一来源）：这里只留**引用别名**，
// 所以下面所有调用点（`std::size(kKnownXxx)` / `JoinKnownFields`）一个字不用改。
// ⚠️ 数组顺序在 Fields.h 里也不能动——报错文案与自检断言依赖它。
constexpr auto& kKnownRootFields = fields::kKnownRootFields;
constexpr auto& kKnownTextFields = fields::kKnownTextFields;
constexpr auto& kKnownShapeFields = fields::kKnownShapeFields;
constexpr auto& kKnownButtonAppearanceFields
    = fields::kKnownButtonAppearanceFields;
constexpr auto& kKnownTransitionFields = fields::kKnownTransitionFields;
constexpr auto& kKnownStrokeFields = fields::kKnownStrokeFields;
constexpr auto& kKnownShadowFields = fields::kKnownShadowFields;
constexpr auto& kKnownPanelFields = fields::kKnownPanelFields;
constexpr auto& kKnownPanelAppearanceFields
    = fields::kKnownPanelAppearanceFields;
constexpr auto& kKnownLabelFields = fields::kKnownLabelFields;
constexpr auto& kKnownLabelTextFields = fields::kKnownLabelTextFields;
constexpr auto& kKnownImageFields = fields::kKnownImageFields;
constexpr auto& kKnownImageAppearanceFields
    = fields::kKnownImageAppearanceFields;
constexpr auto& kKnownTextTableFields = fields::kKnownTextTableFields;
constexpr auto& kKnownThemeFields = fields::kKnownThemeFields;
constexpr auto& kKnownConfigFields = fields::kKnownConfigFields;
constexpr auto& kKnownDesignFields = fields::kKnownDesignFields;
constexpr auto& kKnownWindowFields = fields::kKnownWindowFields;

/// 单张图片的字节上限。烘进源码的字节数与编译时间是线性关系（1 字节约 5 个
/// 源字符），2 MB 已经是"一两万行源码"的量级——再大就该先压一压图，
/// 而不是让每次构建多等几十秒（`CXXCSS.md` §4.1 写了这条上限）。
constexpr std::size_t kMaxImageBytes = 2u * 1024u * 1024u;

/**
 * @brief 读一个图片文件（生成期烘字节那条路）。
 *
 * **成功返回空串，失败返回"给人看的那句原因"**——错误位置由调用方交给
 * `Error(where, ...)`，所以报错照样指在配置的那一行上。做成自由函数是为了让
 * **按钮与图片两条路共用同一份**（两份实现各自演化就会出现"按钮那边拦了大文件、
 * 图片那边没有"这种不一致）。
 *
 * 上限 2 MB（`kMaxImageBytes`）：字节要烘进生成代码，编译时间随字节数线性上涨
 * （`CXXCSS.md` §4.1）。**运行期那条路（配置里 `"runtime": true`）根本不到这里**
 * ——那种图不进 exe，也就没有这个上限。
 */
std::string ReadImageBytes(const std::string& path,
                           std::vector<std::uint8_t>& out) {
    std::error_code error;
    const std::filesystem::path filePath = ToPath(path);
    const std::uintmax_t size = std::filesystem::file_size(filePath, error);
    if (error) {
        return "图片读不到文件 \"" + path + "\"（" + error.message()
             + "）；生成期烘字节那条路要写**绝对路径**，而且文件得真的在。"
               "要让图不进 exe 就写相对资源根的路径 + \"runtime\": true";
    }
    if (size > kMaxImageBytes) {
        return "图片文件太大：" + std::to_string(size) + " 字节，上限是 "
             + std::to_string(kMaxImageBytes)
             + " 字节（2 MB）——字节要烘进生成代码，编译时间随它线性上涨；"
               "先压一压图，或者改用 \"runtime\": true 让它在运行期读";
    }

    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        return "图片打不开文件 \"" + path + "\"";
    }
    out.assign(std::istreambuf_iterator<char>(stream),
               std::istreambuf_iterator<char>());
    if (out.size() != static_cast<std::size_t>(size)) {
        return "图片读文件 \"" + path + "\" 时字节数对不上（读到 "
             + std::to_string(out.size()) + "，期望 " + std::to_string(size)
             + "）";
    }
    return {};
}

/// SVG 子集解析出来的"原始描述"（几何按**组件框**解释，校验放到 Finish()）。
struct RawSvgShape {
    std::string tag;  ///< rect / circle / ellipse / path
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    bool hasFill = false;
    std::uint32_t fill = 0xFF000000u;
    /**
     * `path` 折线化之后的顶点表（`x0,y0,…`，设计坐标）——B2。
     *
     * 曲线（C/S/Q/T/A）在这一步就被离散成折线了：形状层只有"折线"这一种
     * 表达（`polygonPath`），把曲线带进运行期等于在渲染热路径里做数值求解。
     */
    std::vector<float> points;
};

/// 跳过空白。
std::size_t SkipSpace(const std::string& text, std::size_t at) {
    while (at < text.size()
           && std::isspace(static_cast<unsigned char>(text[at]))) {
        ++at;
    }
    return at;
}

/// 读一个标识符（标签名 / 属性名）。
std::string ReadName(const std::string& text, std::size_t& at) {
    const std::size_t begin = at;
    while (at < text.size()
           && (std::isalnum(static_cast<unsigned char>(text[at]))
               || text[at] == '-' || text[at] == '_')) {
        ++at;
    }
    return text.substr(begin, at - begin);
}

/// 把 `#RGB` / `#RRGGBB` / `#RRGGBBAA` / black / white 解析成 0xAARRGGBB。
bool ParseSvgColor(const std::string& text, std::uint32_t& out) {
    if (text == "black") {
        out = 0xFF000000u;
        return true;
    }
    if (text == "white") {
        out = 0xFFFFFFFFu;
        return true;
    }
    if (text.size() < 4 || text[0] != '#') {
        return false;
    }
    const std::string hex = text.substr(1);
    auto hexDigit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (const char ch : hex) {
        if (hexDigit(ch) < 0) {
            return false;
        }
    }
    auto byteAt = [&](std::size_t index) -> unsigned {
        return static_cast<unsigned>(hexDigit(hex[index]));
    };
    if (hex.size() == 3) {  // #RGB → 每位翻倍
        out = 0xFF000000u | ((byteAt(0) * 17u) << 16)
            | ((byteAt(1) * 17u) << 8) | (byteAt(2) * 17u);
        return true;
    }
    if (hex.size() == 6 || hex.size() == 8) {
        const unsigned alpha =
            hex.size() == 8 ? (byteAt(6) * 16u + byteAt(7)) : 255u;
        out = (alpha << 24) | ((byteAt(0) * 16u + byteAt(1)) << 16)
            | ((byteAt(2) * 16u + byteAt(3)) << 8)
            | (byteAt(4) * 16u + byteAt(5));
        return true;
    }
    return false;
}

/// 从 `name="value"` 形式的属性段里取一个属性；没有返回 false。
bool FindSvgAttr(const std::string& attrs, const std::string& name,
                 std::string& out) {
    std::size_t at = 0;
    while (at < attrs.size()) {
        at = SkipSpace(attrs, at);
        const std::string key = ReadName(attrs, at);
        if (key.empty()) {
            ++at;
            continue;
        }
        at = SkipSpace(attrs, at);
        if (at >= attrs.size() || attrs[at] != '=') {
            continue;
        }
        ++at;
        at = SkipSpace(attrs, at);
        if (at >= attrs.size() || (attrs[at] != '"' && attrs[at] != '\'')) {
            continue;
        }
        const char quote = attrs[at++];
        const std::size_t end = attrs.find(quote, at);
        if (end == std::string::npos) {
            break;
        }
        if (key == name) {
            out = attrs.substr(at, end - at);
            return true;
        }
        at = end + 1;
    }
    return false;
}

/// 读一个数字属性；缺失用 fallback，写了但不是数字返回 NaN（调用方报错）。
double SvgNumber(const std::string& attrs, const std::string& name,
                 double fallback) {
    std::string raw;
    if (!FindSvgAttr(attrs, name, raw)) {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str() || (end != nullptr && *end != '\0')) {
        return std::nan("");
    }
    return value;
}

/**
 * SVG `path` 的 `d` 属性解析器（B2）：**折线化**成一张顶点表。
 *
 * 支持的命令（大小写 = 绝对 / 相对，照 SVG 规范）：
 *   - `M/m`（移动到，后面多余的坐标对按 `L` 处理）、`L/l`、`H/h`、`V/v`、`Z/z`；
 *   - `C/c`、`S/s`（三次贝塞尔，含光滑续接）、`Q/q`、`T/t`（二次，含光滑续接）；
 *   - `A/a`（椭圆弧，按 SVG 规范做"端点参数化 → 中心参数化"再采样）。
 *
 * 曲线按**控制多边形长度**估段数（每段不超过 4 个设计单位，段数夹在 4~64），
 * 于是"大图形采样得细、小图形不浪费"。
 *
 * 这一轮**只认单条子路径**：第二个 `M` 会报"多图形 / 孔洞还没做"——因为形状层
 * 现在只有"一条点列"，硬接会把不相连的两块用一条直线连起来（画出来是个怪东西）。
 */
class SvgPathParser {
public:
    SvgPathParser(const std::string& text, std::vector<float>& points)
        : _text(text), _points(points) {}

    /// 解析；返回空字符串 = 成功，否则是错误原因。
    std::string Run() {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size()) {
                break;
            }
            const char command = _text[_at];
            if (!IsCommand(command)) {
                return std::string("认不出的命令 '") + command
                     + "'（支持 M/L/H/V/Z/C/S/Q/T/A，大小写都可以）";
            }
            ++_at;
            const std::string failure = Dispatch(command);
            if (!failure.empty()) {
                return failure;
            }
        }
        if (!_sawMove) {
            return "path 里没有 M（从哪里开始画）";
        }
        if (_points.size() < 6) {
            return "path 折线化之后不足 3 个点（画不出东西）";
        }
        return {};
    }

private:
    static bool IsCommand(char ch) {
        switch (ch) {
            case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
            case 'V': case 'v': case 'Z': case 'z': case 'C': case 'c':
            case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
            case 'A': case 'a':
                return true;
            default:
                return false;
        }
    }

    void SkipSeparators() {
        while (_at < _text.size()
               && (std::isspace(static_cast<unsigned char>(_text[_at]))
                   || _text[_at] == ',')) {
            ++_at;
        }
    }

    bool ReadNumber(double& out) {
        SkipSeparators();
        if (_at >= _text.size()) {
            return false;
        }
        const char* begin = _text.c_str() + _at;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) {
            return false;
        }
        _at += static_cast<std::size_t>(end - begin);
        out = value;
        return true;
    }

    /// 圆弧的两个标志位（0 / 1，SVG 允许不写分隔符）。
    bool ReadFlag(double& out) {
        SkipSeparators();
        if (_at >= _text.size()) {
            return false;
        }
        if (_text[_at] == '0' || _text[_at] == '1') {
            out = _text[_at] == '1' ? 1.0 : 0.0;
            ++_at;
            return true;
        }
        return false;
    }

    void Push(double x, double y) {
        _points.push_back(static_cast<float>(x));
        _points.push_back(static_cast<float>(y));
    }

    std::string Need(const char* what) {
        return std::string("path 在 ") + what + " 那里缺参数";
    }

    std::string Dispatch(char command) {
        const bool relative = std::islower(static_cast<unsigned char>(command));
        const char upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(command)));

        switch (upper) {
            case 'M': {
                double x = 0.0;
                double y = 0.0;
                if (!ReadNumber(x) || !ReadNumber(y)) {
                    return Need("M");
                }
                if (_sawMove) {
                    return "path 里出现了第二个 M：**多图形 / 孔洞还没做**"
                           "（形状层现在只有一条点列；分开画要等那一轮）";
                }
                _sawMove = true;
                _x = relative ? _x + x : x;
                _y = relative ? _y + y : y;
                _startX = _x;
                _startY = _y;
                Push(_x, _y);
                _hasCubicCtrl = false;
                _hasQuadCtrl = false;
                // `M` 后面多余的坐标对按 `L` 处理（SVG 规范）。
                return Lines(relative);
            }
            case 'L':
                return Lines(relative);
            case 'H': {
                while (true) {
                    SkipSeparators();
                    if (_at >= _text.size() || !StartsNumber()) {
                        return {};
                    }
                    double x = 0.0;
                    if (!ReadNumber(x)) {
                        return Need("H");
                    }
                    _x = relative ? _x + x : x;
                    Push(_x, _y);
                    _hasCubicCtrl = false;
                    _hasQuadCtrl = false;
                }
            }
            case 'V': {
                while (true) {
                    SkipSeparators();
                    if (_at >= _text.size() || !StartsNumber()) {
                        return {};
                    }
                    double y = 0.0;
                    if (!ReadNumber(y)) {
                        return Need("V");
                    }
                    _y = relative ? _y + y : y;
                    Push(_x, _y);
                    _hasCubicCtrl = false;
                    _hasQuadCtrl = false;
                }
            }
            case 'Z': {
                // 闭合：把当前点挪回子路径起点。填充时本来就按闭合处理，
                // 所以这里只补一个点（让"闭合边"也参与描边与对账）。
                if (_sawMove) {
                    Push(_startX, _startY);
                    _x = _startX;
                    _y = _startY;
                }
                _hasCubicCtrl = false;
                _hasQuadCtrl = false;
                return {};
            }
            case 'C':
                return Cubics(relative);
            case 'S':
                return SmoothCubics(relative);
            case 'Q':
                return Quadratics(relative);
            case 'T':
                return SmoothQuadratics(relative);
            case 'A':
                return Arcs(relative);
            default:
                return std::string("内部错误：没处理的命令 ") + command;
        }
    }

    bool StartsNumber() {
        if (_at >= _text.size()) {
            return false;
        }
        const char ch = _text[_at];
        return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-'
            || ch == '+' || ch == '.';
    }

    /// `L` 的重复形式：一直读到下一个命令或字符串末尾。
    std::string Lines(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(x) || !ReadNumber(y)) {
                return Need("L");
            }
            _x = relative ? _x + x : x;
            _y = relative ? _y + y : y;
            Push(_x, _y);
            _hasCubicCtrl = false;
            _hasQuadCtrl = false;
        }
    }

    std::string Cubics(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double x1 = 0.0;
            double y1 = 0.0;
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(x1) || !ReadNumber(y1) || !ReadNumber(x2)
                || !ReadNumber(y2) || !ReadNumber(x) || !ReadNumber(y)) {
                return Need("C");
            }
            const double cx1 = relative ? _x + x1 : x1;
            const double cy1 = relative ? _y + y1 : y1;
            const double cx2 = relative ? _x + x2 : x2;
            const double cy2 = relative ? _y + y2 : y2;
            const double ex = relative ? _x + x : x;
            const double ey = relative ? _y + y : y;
            SampleCubic(_x, _y, cx1, cy1, cx2, cy2, ex, ey);
            _lastCubicCtrlX = cx2;
            _lastCubicCtrlY = cy2;
            _hasCubicCtrl = true;
            _hasQuadCtrl = false;
            _x = ex;
            _y = ey;
        }
    }

    std::string SmoothCubics(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(x2) || !ReadNumber(y2) || !ReadNumber(x)
                || !ReadNumber(y)) {
                return Need("S");
            }
            // 光滑续接：第一个控制点是"上一个控制点关于当前点的镜像"。
            const double cx1 =
                _hasCubicCtrl ? 2.0 * _x - _lastCubicCtrlX : _x;
            const double cy1 =
                _hasCubicCtrl ? 2.0 * _y - _lastCubicCtrlY : _y;
            const double cx2 = relative ? _x + x2 : x2;
            const double cy2 = relative ? _y + y2 : y2;
            const double ex = relative ? _x + x : x;
            const double ey = relative ? _y + y : y;
            SampleCubic(_x, _y, cx1, cy1, cx2, cy2, ex, ey);
            _lastCubicCtrlX = cx2;
            _lastCubicCtrlY = cy2;
            _hasCubicCtrl = true;
            _hasQuadCtrl = false;
            _x = ex;
            _y = ey;
        }
    }

    std::string Quadratics(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double x1 = 0.0;
            double y1 = 0.0;
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(x1) || !ReadNumber(y1) || !ReadNumber(x)
                || !ReadNumber(y)) {
                return Need("Q");
            }
            const double cx = relative ? _x + x1 : x1;
            const double cy = relative ? _y + y1 : y1;
            const double ex = relative ? _x + x : x;
            const double ey = relative ? _y + y : y;
            SampleQuadratic(_x, _y, cx, cy, ex, ey);
            _lastQuadCtrlX = cx;
            _lastQuadCtrlY = cy;
            _hasQuadCtrl = true;
            _hasCubicCtrl = false;
            _x = ex;
            _y = ey;
        }
    }

    std::string SmoothQuadratics(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(x) || !ReadNumber(y)) {
                return Need("T");
            }
            const double cx = _hasQuadCtrl ? 2.0 * _x - _lastQuadCtrlX : _x;
            const double cy = _hasQuadCtrl ? 2.0 * _y - _lastQuadCtrlY : _y;
            const double ex = relative ? _x + x : x;
            const double ey = relative ? _y + y : y;
            SampleQuadratic(_x, _y, cx, cy, ex, ey);
            _lastQuadCtrlX = cx;
            _lastQuadCtrlY = cy;
            _hasQuadCtrl = true;
            _hasCubicCtrl = false;
            _x = ex;
            _y = ey;
        }
    }

    std::string Arcs(bool relative) {
        while (true) {
            SkipSeparators();
            if (_at >= _text.size() || !StartsNumber()) {
                return {};
            }
            double rx = 0.0;
            double ry = 0.0;
            double rotation = 0.0;
            double largeArc = 0.0;
            double sweep = 0.0;
            double x = 0.0;
            double y = 0.0;
            if (!ReadNumber(rx) || !ReadNumber(ry) || !ReadNumber(rotation)
                || !ReadFlag(largeArc) || !ReadFlag(sweep) || !ReadNumber(x)
                || !ReadNumber(y)) {
                return Need("A");
            }
            const double ex = relative ? _x + x : x;
            const double ey = relative ? _y + y : y;
            SampleArc(rx, ry, rotation, largeArc >= 0.5, sweep >= 0.5, ex, ey);
            _hasCubicCtrl = false;
            _hasQuadCtrl = false;
            _x = ex;
            _y = ey;
        }
    }

    /// 三次贝塞尔采样（不含起点，含终点）。
    void SampleCubic(double x0, double y0, double x1, double y1, double x2,
                     double y2, double x3, double y3) {
        const double length = std::hypot(x1 - x0, y1 - y0)
                            + std::hypot(x2 - x1, y2 - y1)
                            + std::hypot(x3 - x2, y3 - y2);
        const int steps = StepCount(length);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double u = 1.0 - t;
            const double bx = u * u * u * x0 + 3.0 * u * u * t * x1
                            + 3.0 * u * t * t * x2 + t * t * t * x3;
            const double by = u * u * u * y0 + 3.0 * u * u * t * y1
                            + 3.0 * u * t * t * y2 + t * t * t * y3;
            Push(bx, by);
        }
    }

    /// 二次贝塞尔采样（升级成三次再采，省一份公式）。
    void SampleQuadratic(double x0, double y0, double x1, double y1, double x2,
                         double y2) {
        // 二次 → 三次：控制点按 2/3 权重摊开。
        const double c1x = x0 + (2.0 / 3.0) * (x1 - x0);
        const double c1y = y0 + (2.0 / 3.0) * (y1 - y0);
        const double c2x = x2 + (2.0 / 3.0) * (x1 - x2);
        const double c2y = y2 + (2.0 / 3.0) * (y1 - y2);
        SampleCubic(x0, y0, c1x, c1y, c2x, c2y, x2, y2);
    }

    /**
     * 椭圆弧采样：照 SVG 规范做"端点参数化 → 中心参数化"。
     *
     * 这一步不做的话，`A` 命令就只能画直线——那是最常见的"图标圆角"
     * （`M...A...A...Z` 画一个圆角矩形），漏了它 path 基本没用。
     */
    void SampleArc(double rx, double ry, double rotationDegrees, bool largeArc,
                   bool sweep, double x2, double y2) {
        const double x1 = _x;
        const double y1 = _y;
        rx = std::fabs(rx);
        ry = std::fabs(ry);
        if (rx <= 0.0 || ry <= 0.0 || (x1 == x2 && y1 == y2)) {
            Push(x2, y2);  // 退化：当直线
            return;
        }

        const double phi = rotationDegrees * 3.14159265358979323846 / 180.0;
        const double cosPhi = std::cos(phi);
        const double sinPhi = std::sin(phi);

        // 1. 中点差，旋转到椭圆主轴坐标系。
        const double dx = (x1 - x2) * 0.5;
        const double dy = (y1 - y2) * 0.5;
        const double x1p = cosPhi * dx + sinPhi * dy;
        const double y1p = -sinPhi * dx + cosPhi * dy;

        // 2. 半径不够就按规范放大。
        const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1.0) {
            const double scale = std::sqrt(lambda);
            rx *= scale;
            ry *= scale;
        }

        // 3. 中心（在主轴坐标系里）。
        const double rx2 = rx * rx;
        const double ry2 = ry * ry;
        double numerator = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        if (numerator < 0.0) {
            numerator = 0.0;
        }
        const double denominator = rx2 * y1p * y1p + ry2 * x1p * x1p;
        double factor = denominator > 0.0 ? std::sqrt(numerator / denominator)
                                          : 0.0;
        if (largeArc == sweep) {
            factor = -factor;
        }
        const double cxp = factor * rx * y1p / ry;
        const double cyp = -factor * ry * x1p / rx;
        const double cx = cosPhi * cxp - sinPhi * cyp + (x1 + x2) * 0.5;
        const double cy = sinPhi * cxp + cosPhi * cyp + (y1 + y2) * 0.5;

        // 4. 起止角与扫角。
        const auto angleOf = [](double ux, double uy, double vx, double vy) {
            const double dot = ux * vx + uy * vy;
            const double length = std::hypot(ux, uy) * std::hypot(vx, vy);
            double value = length > 0.0 ? dot / length : 1.0;
            value = std::clamp(value, -1.0, 1.0);
            double angle = std::acos(value);
            if (ux * vy - uy * vx < 0.0) {
                angle = -angle;
            }
            return angle;
        };
        const double startAngle = angleOf(1.0, 0.0, (x1p - cxp) / rx,
                                          (y1p - cyp) / ry);
        double deltaAngle = angleOf((x1p - cxp) / rx, (y1p - cyp) / ry,
                                    (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!sweep && deltaAngle > 0.0) {
            deltaAngle -= 2.0 * 3.14159265358979323846;
        } else if (sweep && deltaAngle < 0.0) {
            deltaAngle += 2.0 * 3.14159265358979323846;
        }

        // 5. 采样：按弧长估段数（用平均半径）。
        const double arcLength =
            std::fabs(deltaAngle) * (rx + ry) * 0.5;
        const int steps = StepCount(arcLength);
        for (int i = 1; i <= steps; ++i) {
            const double t = startAngle + deltaAngle
                                           * (static_cast<double>(i) / steps);
            const double px = cx + rx * std::cos(t) * cosPhi
                            - ry * std::sin(t) * sinPhi;
            const double py = cy + rx * std::cos(t) * sinPhi
                            + ry * std::sin(t) * cosPhi;
            Push(px, py);
        }
    }

    /// 段数：每段不超过 4 个设计单位，夹在 4~64。
    static int StepCount(double length) {
        const double rough = std::ceil(length / 4.0);
        if (rough < 4.0) {
            return 4;
        }
        if (rough > 64.0) {
            return 64;
        }
        return static_cast<int>(rough);
    }

    const std::string& _text;
    std::vector<float>& _points;
    std::size_t _at = 0;
    bool _sawMove = false;
    double _x = 0.0;
    double _y = 0.0;
    double _startX = 0.0;
    double _startY = 0.0;
    double _lastCubicCtrlX = 0.0;
    double _lastCubicCtrlY = 0.0;
    bool _hasCubicCtrl = false;
    double _lastQuadCtrlX = 0.0;
    double _lastQuadCtrlY = 0.0;
    bool _hasQuadCtrl = false;
};

/**
 * 极小的 SVG 子集解析：**只认一个**图形元素（`rect` / `circle` / `ellipse`）。
 *
 * 边界写进 `CXXCSS.md` §4.1 了。要点：
 *   - 认 `rect`（可带 `rx`/`ry`）、`circle`、`ellipse`，各一个；
 *   - **不认** `path` / `g` / `line` / `polygon` / `use` / 变换 / 描边 / 渐变——
 *     报错时说清"是没实现，不是写错了"；
 *   - 填充色只认 `#RGB` / `#RRGGBB` / `#RRGGBBAA` 与黑/白（缺省黑，与 SVG 一致）。
 *
 * @return 空字符串 = 成功；否则是给用户看的错误原因（不含字段名）。
 */
std::string ParseSvgSubset(const std::string& svg, RawSvgShape& out) {
    // 去掉注释与处理指令（`<?xml ...?>`）：它们不影响子集语义。
    std::string text;
    for (std::size_t at = 0; at < svg.size();) {
        if (svg.compare(at, 4, "<!--") == 0) {
            const std::size_t end = svg.find("-->", at);
            if (end == std::string::npos) {
                return "注释没有闭合（少了 -->）";
            }
            at = end + 3;
            continue;
        }
        if (svg.compare(at, 2, "<?") == 0) {
            const std::size_t end = svg.find("?>", at);
            if (end == std::string::npos) {
                return "处理指令没有闭合（少了 ?>）";
            }
            at = end + 2;
            continue;
        }
        text.push_back(svg[at]);
        ++at;
    }

    std::size_t at = 0;
    bool sawRoot = false;
    bool sawElement = false;
    while (at < text.size()) {
        const std::size_t open = text.find('<', at);
        if (open == std::string::npos) {
            break;
        }
        if (text.compare(open, 4, "</svg") == 0) {
            break;  // 根元素闭合
        }
        if (text[open + 1] == '/') {
            at = open + 2;  // 其它闭合标签：子集里不该有，忽略掉
            continue;
        }
        std::size_t cursor = open + 1;
        const std::string name = ReadName(text, cursor);
        if (name.empty()) {
            at = open + 1;
            continue;
        }
        const std::size_t close = text.find('>', cursor);
        if (close == std::string::npos) {
            return "标签没有闭合（少了 >）";
        }
        const std::string attrs = text.substr(cursor, close - cursor);

        if (!sawRoot) {
            if (name != "svg") {
                return "第一个元素必须是 <svg>（现在是 <" + name + ">）";
            }
            sawRoot = true;
        } else if (name == "title" || name == "desc" || name == "defs") {
            // 无害的辅助标签：跳过（defs 里的内容不做处理）。
        } else {
            if (name != "rect" && name != "circle" && name != "ellipse"
                && name != "path") {
                return "不支持的标签 <" + name
                     + ">（子集只认 rect / circle / ellipse / path；"
                       "g / line / polygon / use 这些还没有形状词汇能表达，"
                       "是**没实现**而不是你写错了）";
            }
            if (sawElement) {
                return "子集只认**一个**图形元素（第二个是 <" + name
                     + ">）；多图形外观要等「一件外观多个形状」那一轮";
            }

            out.tag = name;
            if (name == "rect") {
                out.x = SvgNumber(attrs, "x", 0.0);
                out.y = SvgNumber(attrs, "y", 0.0);
                out.w = SvgNumber(attrs, "width", 0.0);
                out.h = SvgNumber(attrs, "height", 0.0);
                out.rx = SvgNumber(attrs, "rx", 0.0);
                out.ry = SvgNumber(attrs, "ry", 0.0);
            } else if (name == "circle") {
                out.cx = SvgNumber(attrs, "cx", 0.0);
                out.cy = SvgNumber(attrs, "cy", 0.0);
                out.rx = SvgNumber(attrs, "r", 0.0);
            } else if (name == "path") {
                std::string data;
                if (!FindSvgAttr(attrs, "d", data)) {
                    return "path 缺 d（形状数据）";
                }
                SvgPathParser parser(data, out.points);
                const std::string pathError = parser.Run();
                if (!pathError.empty()) {
                    return pathError;
                }
                // 填色的处理与其它标签共用下面那一段。
            } else {
                out.cx = SvgNumber(attrs, "cx", 0.0);
                out.cy = SvgNumber(attrs, "cy", 0.0);
                out.rx = SvgNumber(attrs, "rx", 0.0);
                out.ry = SvgNumber(attrs, "ry", 0.0);
            }
            for (const double value :
                 {out.x, out.y, out.w, out.h, out.cx, out.cy, out.rx, out.ry}) {
                if (std::isnan(value)) {
                    return "几何属性必须是数字（有一项不是）";
                }
            }

            std::string fill;
            if (FindSvgAttr(attrs, "fill", fill)) {
                if (fill == "none") {
                    return "fill=\"none\" 现在画不出东西（描边还没落地）；"
                           "给个颜色，或者先别用 svg";
                }
                if (!ParseSvgColor(fill, out.fill)) {
                    return "认不出 fill=\"" + fill
                         + "\"（只认 #RGB / #RRGGBB / #RRGGBBAA / black / white）";
                }
                out.hasFill = true;
            }
            sawElement = true;
        }
        at = close + 1;
    }

    if (!sawRoot) {
        return "没找到 <svg> 根元素";
    }
    if (!sawElement) {
        return "svg 里没有图形（子集只认 rect / circle / ellipse / path）";
    }
    return {};
}

/// 生成期用的"两个形状是不是同一个"。
///
/// 和运行期那份（`InkingDraw.cpp` 的 `SameShape`）同一个教训：**所有影响几何的
/// 字段都要比**。运行期那份漏过 B3/B5 加的字段，这份也漏过——一起补上。
/// 顶点表按内容比（借的是视图，比指针说明不了形状相同）。
bool SameShape(const inking::ShapeSpec& a, const inking::ShapeSpec& b) {
    if (a.kind != b.kind || a.radius != b.radius
        || a.thickness != b.thickness || a.cap != b.cap
        || a.sides != b.sides || a.rotation != b.rotation
        || a.chamfer != b.chamfer || a.points != b.points
        || a.innerRatio != b.innerRatio || a.exponent != b.exponent
        || a.startAngle != b.startAngle || a.sweepAngle != b.sweepAngle
        || a.pathPointCount != b.pathPointCount) {
        return false;
    }
    for (int i = 0; i < a.pathPointCount * 2; ++i) {
        if (a.pathPoints[i] != b.pathPoints[i]) {
            return false;
        }
    }
    return true;
}

/**
 * svg 子集 → 形状。
 *
 * **几何必须正好对得上组件框**（rect 铺满、circle/ellipse 居中且用满短边/整框）：
 * 子集的几何会成为**这个组件的形状**，而形状要同时喂给渲染与命中——偏移或缩放
 * 需要"边框 / 变换"那套词汇，这一轮没有，所以宁可报错也不悄悄挪一下。
 */
bool SvgToShape(const RawSvgShape& raw, int width, int height,
                inking::ShapeSpec& out, std::string& error,
                std::vector<float>* pathOut = nullptr) {
    const double w = static_cast<double>(width);
    const double h = static_cast<double>(height);
    constexpr double kEps = 1e-6;
    const auto same = [](double a, double b) {
        return std::fabs(a - b) <= kEps;
    };

    if (raw.tag == "rect") {
        if (!same(raw.x, 0.0) || !same(raw.y, 0.0) || !same(raw.w, w)
            || !same(raw.h, h)) {
            error = "rect 要正好铺满组件（x=0、y=0、width=" + std::to_string(width)
                  + "、height=" + std::to_string(height)
                  + "）；子集不支持偏移或缩放";
            return false;
        }
        const double radius = std::max(raw.rx, raw.ry);
        out = radius > 0.0
                  ? inking::ShapeSpec::RoundedRect(static_cast<float>(radius))
                  : inking::ShapeSpec::Rect();
        return true;
    }
    if (raw.tag == "circle") {
        const double radius = std::min(w, h) * 0.5;
        if (!same(raw.cx, w * 0.5) || !same(raw.cy, h * 0.5)
            || !same(raw.rx, radius)) {
            error = "circle 要正好居中、半径取 min(宽,高)/2（cx="
                  + std::to_string(w * 0.5) + "、cy=" + std::to_string(h * 0.5)
                  + "、r=" + std::to_string(radius) + "）";
            return false;
        }
        out = inking::ShapeSpec::Circle();
        return true;
    }
    if (raw.tag == "ellipse") {
        if (!same(raw.cx, w * 0.5) || !same(raw.cy, h * 0.5)
            || !same(raw.rx, w * 0.5) || !same(raw.ry, h * 0.5)) {
            error = "ellipse 要正好用满组件框（cx=" + std::to_string(w * 0.5)
                  + "、cy=" + std::to_string(h * 0.5)
                  + "、rx=" + std::to_string(w * 0.5)
                  + "、ry=" + std::to_string(h * 0.5) + "）";
            return false;
        }
        out = inking::ShapeSpec::Ellipse();
        return true;
    }
    if (raw.tag == "path") {
        // `path` 是**任意**形状：它不需要与组件框一致（点列就是设计坐标，
        // 可以只占框里一小块）。点列交给调用方持有（`ShapeSpec` 里只有视图），
        // `Emit` 会把它烘成生成代码里的静态数组。
        if (pathOut == nullptr) {
            error = "path 需要调用方持有顶点表（内部错误：没有传点列存储）";
            return false;
        }
        if (raw.points.size() < 6) {
            error = "path 折线化之后不足 3 个点";
            return false;
        }
        *pathOut = raw.points;
        out = inking::ShapeSpec::PolygonPath(
            pathOut->data(), static_cast<int>(pathOut->size() / 2));
        return true;
    }
    error = "内部错误：不认识的 svg 标签 \"" + raw.tag + "\"";
    return false;
}

bool IsKnownField(const char* const* table, std::size_t count,
                  const std::string& key) {
    for (std::size_t i = 0; i < count; ++i) {
        if (key == table[i]) {
            return true;
        }
    }
    return false;
}

/// 把"已知字段"拼成一句给用户看的提示。
std::string JoinKnownFields(const char* const* table, std::size_t count) {
    std::string joined;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            joined += "、";
        }
        joined += table[i];
    }
    return joined;
}

/**
 * **生成器指令字段**：不参与运行期数据，所有加载器一律跳过（0.2.0 T-10）。
 *
 * 与"未知字段"的区别是**谁处理它**：指令字段在读配置这一层就被消费掉了
 * （`$comment` 是注释、`$extends` 是继承），不会进 `ButtonData` / `PanelData`
 * 那些结构，所以白名单里也不该有它们。
 */
bool IsDirectiveField(const std::string& key) {
    return key == "$comment" || key == "$extends";
}

/**
 * @brief 配置加载器的**共用那一半**：报错、提醒，以及各种通用字段读取器。
 *
 * 两个 loader（`ButtonLoader` / `PanelLoader`）都继承它。为什么抽出来：这些读取器
 * 各写一遍就会分叉——同一句"`transition` 写了 `easing` 却没写 `duration`"，
 * 按钮那边报错、面板那边悄悄放行，正是"抄第二遍"最典型的代价
 * （`include/ink/basic/InkingDraw.h` 头顶那条同一条理由）。
 *
 * 读出来的值都走**出参**，不直接写某个 config：基类不认识"按钮"或"面板"，
 * 它只负责"这个字段合不合法、值是多少"。
 */
class ConfigReader {
public:
    ConfigReader(std::string path, LoadResult& result)
        : _path(std::move(path)), _result(result) {}

protected:
    std::string _path;
    LoadResult& _result;
    bool _failed = false;
    std::vector<std::string> _warnings;

    void Error(const JsonValue& where, const std::string& message) {
        // 值的来源文件优先（`$extends` 合并进来的字段来自父文件，T-10）：
        // 只报子文件名会让人去错误的地方找。
        _result.errors.push_back(
            CxxcssError{where.file.empty() ? _path : where.file,
                        where.where.line, message});
        _failed = true;
    }

    void Warn(const std::string& message) {
        _warnings.push_back(message);
    }

    /// 数字（只认 JSON 数字，不认字符串）。
    double ReadNumber(const JsonValue& value, const std::string& field) {
        if (!value.IsNumber()) {
            Error(value, field + " 必须是数字（不要加引号），这里是"
                             + value.KindName());
            return 0.0;
        }
        return value.number;
    }

    /**
     * 尺寸：数字或数字字符串（历史遗留写法，CXXCSS.md §2.1 两种都收）。
     *
     * `allowZero` 是给**标签的高度**开的口子：`0` 表示"高度由内容撑"，
     * 与"省略这个字段"是同一件事（`CXXCSS.md` §3.8 就是这么写的）。
     * 别的尺寸写 0 仍然报错——宽度 0 没有意义（它是换行的依据），
     * 容器高 0 只会让人觉得"内容凭空消失了"。
     */
    int ReadSize(const JsonValue& value, const std::string& field,
                 bool allowZero = false) {
        double number = 0.0;
        if (value.IsNumber()) {
            number = value.number;
        } else if (value.IsString()) {
            char* end = nullptr;
            number = std::strtod(value.text.c_str(), &end);
            if (end == nullptr || *end != '\0' || value.text.empty()) {
                Error(value, field + " 的字符串写法必须是纯数字，这里是 \""
                                 + value.text + "\"");
                return 0;
            }
        } else {
            Error(value, field + " 必须是数字，这里是" + value.KindName());
            return 0;
        }

        if (allowZero ? (number < 0.0) : (number < 1.0)) {
            Error(value,
                  field + (allowZero ? " 不能是负数（现在是 " : " 必须大于 0（现在是 ")
                      + std::to_string(number) + "）");
            return 0;
        }
        return static_cast<int>(number + 0.5);
    }

    void ReadAnchor(const JsonValue& value, const std::string& field,
                    float& out) {
        const double number = ReadNumber(value, field);
        out = static_cast<float>(number);
        // 锚点允许任意浮点，但超出 [0,1] 的行为未定义（CXXCSS.md §2.2），提醒一句。
        if (number < 0.0 || number > 1.0) {
            Warn(field + " = " + std::to_string(number)
                 + " 落在 [0,1] 之外，越界行为未定义");
        }
    }

    /**
     * 这一批文件里装好的**主题表**（最多一份；`LoadConfigs` 会先收主题再收组件）。
     * 没有主题表时返回 `nullptr`——那时 `$token` 要明确报错，不能悄悄用兜底值。
     */
    const ThemeConfig* Theme() const noexcept {
        return _result.themes.empty() ? nullptr : &_result.themes.front();
    }

    /// 主题里有哪些颜色 token（报错文案里按书写顺序列出来）。
    static std::string JoinTokens(
        const std::vector<std::pair<std::string, std::uint32_t>>& tokens) {
        std::string joined;
        for (const auto& entry : tokens) {
            if (!joined.empty()) {
                joined += " / ";
            }
            joined += "$" + entry.first;
        }
        return joined.empty() ? std::string("（一个都没有）") : joined;
    }

    /// 主题里有哪些字号 token。
    static std::string JoinTokens(
        const std::vector<std::pair<std::string, double>>& tokens) {
        std::string joined;
        for (const auto& entry : tokens) {
            if (!joined.empty()) {
                joined += " / ";
            }
            joined += "$" + entry.first;
        }
        return joined.empty() ? std::string("（一个都没有）") : joined;
    }

    /**
     * `"$surface"` → 主题里的颜色（B8）。
     *
     * 两条规矩：**没有主题表就报错**（"没有主题就用兜底色"会让"主题没生效"
     * 表现成"颜色不太对"，那是最难查的一类）；**token 不存在也报错**，
     * 并把主题里有的 token 列出来。
     */
    std::uint32_t ResolveColorToken(const JsonValue& where,
                                    const std::string& field,
                                    const std::string& token) {
        const ThemeConfig* theme = Theme();
        if (theme == nullptr) {
            Error(where, field + " 用了语义色 \"$" + token
                             + "\"，但这一批里没有主题表——把 "
                               "CXXCSS/Theme/*.json 一起传进来（CXXCSS.md §3.9）");
            return 0;
        }
        const std::uint32_t* color = theme->FindColor(token);
        if (color == nullptr) {
            Error(where, "主题 \"" + theme->name + "\" 里没有颜色 token \"$"
                             + token + "\"（它有的是：" + JoinTokens(theme->colors)
                             + "）");
            return 0;
        }
        return *color;
    }

    /**
     * 把一个 token 名驻留进来并记账。
     *
     * 为什么要驻留：生成的 `themeColor` 是 `const char*`，发的是**字符串字面量**；
     * 而这里拿到的是 JSON 里的 `std::string`，生命周期不保证覆盖发射阶段。
     * 存进 `LoadResult::internedTokens`（`deque`，扩容不失效）就有了稳定地址。
     * 顺便去重记账，供"每张主题表都得有这些 token"的校验用。
     */
    const char* InternColorToken(const std::string& token) {
        if (std::find(_result.usedColorTokens.begin(),
                      _result.usedColorTokens.end(), token)
            == _result.usedColorTokens.end()) {
            _result.usedColorTokens.push_back(token);
        }
        _result.internedTokens.push_back(token);
        return _result.internedTokens.back().c_str();
    }

    /// `"0|0|0|0.75"` → 0xAARRGGBB，或 `"$surface"` → 主题里的颜色（B8）。
    std::uint32_t ReadColorSource(const JsonValue& value,
                                  const std::string& field,
                                  bool isSourceField = true,
                                  const char** tokenOut = nullptr) {
        // 报错文案里的位置名：组件外观写的是 `normal.source`，全局配置写的是
        // `window.background`（那里没有 `.source` 这一层）。
        const std::string label = field + (isSourceField ? ".source" : "");
        if (!value.IsString()) {
            Error(value, label + " 必须是字符串，这里是" + value.KindName());
            return 0;
        }

        // 语义色：`$` 开头的那一串走主题表（其余照旧是 `r|g|b|a`）。
        if (!value.text.empty() && value.text[0] == '$') {
            const std::string token = value.text.substr(1);
            if (tokenOut != nullptr) {
                // 名字留下来，运行期换肤才有得查（`theme/InkingTheme.h`）。
                *tokenOut = InternColorToken(token);
            }
            return ResolveColorToken(value, label, token);
        }

        std::vector<double> parts;
        std::string token;
        const std::string& text = value.text;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '|') {
                if (token.empty()) {
                    Error(value, label + " 里有空的通道（\"" + text + "\"）");
                    return 0;
                }
                char* end = nullptr;
                const double parsed = std::strtod(token.c_str(), &end);
                if (end == nullptr || *end != '\0') {
                    Error(value, label + " 里的 \"" + token + "\" 不是数字（\""
                                     + text + "\"）");
                    return 0;
                }
                parts.push_back(parsed);
                token.clear();
            } else {
                token.push_back(text[i]);
            }
        }

        if (parts.size() != 3 && parts.size() != 4) {
            Error(value, label + " \"" + text + "\" 要有 3 或 4 个通道"
                             "（r|g|b 或 r|g|b|a），现在有 "
                             + std::to_string(parts.size()) + " 个");
            return 0;
        }

        for (double part : parts) {
            if (part < 0.0 || part > 1.0) {
                Warn(label + " 里的通道 " + std::to_string(part)
                     + " 落在 [0,1] 之外，会被夹住");
            }
        }

        const float alpha = parts.size() == 4 ? static_cast<float>(parts[3])
                                              : 1.0f;
        return inking::FromUnitRgba(static_cast<float>(parts[0]),
                                 static_cast<float>(parts[1]),
                                 static_cast<float>(parts[2]), alpha);
    }

    /**
     * 字号（B8）：**数字**，或 `"$body"` 这样的**语义字号**（走主题表）。
     *
     * 与颜色同一个道理：字号写成语义名之后，"正文 15 / 标题 24"这种决定只剩
     * 主题表里一处，改字号不用翻每个组件的 json。校验与 `ReadNumber` 一致
     * （必须大于 0）——语义名解析出来的值也照查一遍。
     */
    float ReadFontSize(const JsonValue& value, const std::string& field) {
        double number = 0.0;
        if (value.IsNumber()) {
            number = value.number;
        } else if (value.IsString()) {
            if (value.text.empty() || value.text[0] != '$') {
                Error(value, field
                                 + " 的字符串写法必须是语义字号（形如 \"$body\"），"
                                   "这里是 \"" + value.text + "\"");
                return 0.0f;
            }
            const std::string token = value.text.substr(1);
            const ThemeConfig* theme = Theme();
            if (theme == nullptr) {
                Error(value, field + " 用了语义字号 \"$" + token
                                     + "\"，但这一批里没有主题表——把 "
                                       "CXXCSS/Theme/*.json 一起传进来"
                                       "（CXXCSS.md §3.9）");
                return 0.0f;
            }
            const double* size = theme->FindFontSize(token);
            if (size == nullptr) {
                Error(value, "主题 \"" + theme->name + "\" 里没有字号 token \"$"
                                 + token + "\"（它有的是："
                                 + JoinTokens(theme->fontSizes) + "）");
                return 0.0f;
            }
            number = *size;
        } else {
            Error(value, field + " 要么是数字，要么是 \"$token\" 语义字号，这里是"
                             + value.KindName());
            return 0.0f;
        }

        if (number <= 0.0) {
            Error(value, field + " 必须大于 0（现在是 " + std::to_string(number)
                             + "）");
            return 0.0f;
        }
        return static_cast<float>(number);
    }

    /**
     * `"transition": { "duration": 0.15, "easing": "easeOut" }`
     * —— 进入这个状态时的过渡：`duration` 是时长（秒），`easing` 是缓动曲线。
     *
     * 两个字段**互相独立**（TaskGuide.md 的 T-2）：曲线只改变进度，不改时长、
     * 起点、终点。`duration` 必须有且大于 0（不想要过渡就别写 `transition`）；
     * `easing` 可选，缺省 linear。
     *
     * 写了 `easing` 却没写 `duration` 要**报错**：没有时长就没有过渡，那条曲线
     * 一点作用都没有——这是"跑起来了但没生效"那一类，最容易骗过人。
     */
    void ReadTransition(const JsonValue& value, const std::string& field,
                        float& seconds, inking::Easing& easing) {
        if (!value.IsObject()) {
            Error(value,
                  field
                      + ".transition 必须是对象，形如 "
                        "{\"duration\":0.15,\"easing\":\"easeOut\"}");
            return;
        }

        // 先收字段再校验：easing 的"有没有配 duration"要看整个对象，
        // 不能边遍历边判（json 里字段顺序不该有意义）。
        const JsonValue* durationField = nullptr;
        const JsonValue* easingField = nullptr;
        for (const auto& entry : value.fields) {
            if (entry.first == "duration") {
                durationField = &entry.second;
            } else if (entry.first == "easing") {
                easingField = &entry.second;
            } else {
                // 文案从白名单数组来（Fields.h 的单一来源）：加字段时改一处，
                // 报错与文档表一起变。
                Error(entry.second,
                      field + ".transition 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 "
                          + JoinKnownFields(kKnownTransitionFields,
                                            std::size(kKnownTransitionFields))
                          + "）");
            }
        }

        if (durationField != nullptr) {
            const double value
                = ReadNumber(*durationField, field + ".transition.duration");
            if (value <= 0.0) {
                Error(*durationField,
                      field + ".transition.duration 必须大于 0（现在是 "
                          + std::to_string(value)
                          + "）；不想要过渡就别写 transition");
            } else {
                seconds = static_cast<float>(value);
            }
        }

        if (easingField == nullptr) {
            return;
        }
        if (!easingField->IsString()) {
            Error(*easingField,
                  field + ".transition.easing 必须是字符串（linear / easeIn / "
                          "easeOut / easeInOut）");
            return;
        }
        const std::string& name = easingField->text;
        if (name == "linear") {
            easing = inking::Easing::Linear;
        } else if (name == "easeIn") {
            easing = inking::Easing::EaseIn;
        } else if (name == "easeOut") {
            easing = inking::Easing::EaseOut;
        } else if (name == "easeInOut") {
            easing = inking::Easing::EaseInOut;
        } else {
            Error(*easingField,
                  field + ".transition.easing \"" + name
                      + "\" 不认识（只支持 linear、easeIn、easeOut、easeInOut）");
            return;
        }
        if (durationField == nullptr) {
            Error(*easingField,
                  field + ".transition 写了 easing 却没写 duration：没有时长就"
                          "没有过渡，这条曲线不会生效（不想要过渡就别写 "
                          "transition）");
        }
    }

    /**
     * `"transform": { "translateX": 0, "translateY": -4, "rotate": 0, "scale": 1.05 }`
     * —— **变换通道**（平移 / 旋转 / 缩放）。四个字段都可选，不写就是单位变换。
     *
     * 它**不改本地几何、也不改命中表**（`docs/InputDesign.md` §11：表按本地形状烘，
     * 查询时把点反变换回本地空间），所以**静态档也能配**——这是"静态按钮也能
     * 移动 / 旋转"、也是"容器开合靠平移"的根据。
     *
     * 两条约定（运行期同一份定义在 `include/ink/dataStruct/InkingTransform.h`）：
     *   - 原点是**组件中心**（等价 CSS 的 `transform-origin: 50% 50%`）；
     *   - `rotate` 的单位是**度**，顺时针为正。
     */
    void ReadTransform(const JsonValue& value, const std::string& field,
                       inking::TransformSpec& out) {
        if (!value.IsObject()) {
            Error(value,
                  field + ".transform 必须是对象，形如 "
                          "{\"translateY\":-4,\"scale\":1.05}");
            return;
        }

        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;

            if (key == "translateX") {
                out.translateX = static_cast<float>(
                    ReadNumber(child, field + ".transform.translateX"));
            } else if (key == "translateY") {
                out.translateY = static_cast<float>(
                    ReadNumber(child, field + ".transform.translateY"));
            } else if (key == "rotate") {
                out.rotate = static_cast<float>(
                    ReadNumber(child, field + ".transform.rotate"));
            } else if (key == "scale") {
                const double scale
                    = ReadNumber(child, field + ".transform.scale");
                if (scale == 0.0) {
                    Error(child, field + ".transform.scale 不能是 0"
                                     "（那会把形状压成一个点；要「消失」"
                                     "请缩到 0.01 之类）");
                    continue;
                }
                out.scale = static_cast<float>(scale);
            } else {
                Error(child,
                      field + ".transform 里的字段 \"" + key
                          + "\" 不认识（只认 translateX、translateY、rotate、scale）");
            }
        }
    }

    /// 形状层（`CXXCSS.md` §3）：一个 `type` 分支派生出 SDF / 命中 / AABB。
    void ReadShape(const JsonValue& value, inking::ShapeSpec& out,
                   std::vector<float>* pathPoints = nullptr) {
        if (!value.IsObject()) {
            Error(value, "shape 必须是对象，形如 "
                         "{\"type\":\"roundedRect\",\"radius\":10}");
            return;
        }

        const JsonValue* type = value.Find("type");
        if (type == nullptr) {
            // shape 写成 {} 等价于直角矩形（CXXCSS.md §3.2）。
            out = inking::ShapeSpec::Rect();
            return;
        }
        if (!type->IsString()) {
            Error(*type, "shape.type 必须是字符串");
            return;
        }

        for (const auto& entry : value.fields) {
            static const std::size_t count =
                sizeof(kKnownShapeFields) / sizeof(kKnownShapeFields[0]);
            if (!IsKnownField(kKnownShapeFields, count, entry.first)) {
                Error(entry.second,
                      "shape 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 type / radius / "
                            "thickness / cap / sides / rotation / chamfer / "
                            "points / innerRatio / exponent / startAngle / "
                            "sweepAngle）");
            }
        }

        // 线宽（ring 的环宽 / line 的线宽）：设计坐标，必须大于 0。
        // 返回 false 表示已经报错，调用处直接收手（不构造带 0 的形状）。
        const auto readThickness = [&](const char* shapeName, float& thickness) {
            const JsonValue* field = value.Find("thickness");
            if (field == nullptr) {
                Error(*type, std::string(shapeName)
                                 + " 需要 thickness（设计坐标，必须大于 0）");
                return false;
            }
            const double number = ReadNumber(*field, "shape.thickness");
            if (number <= 0.0) {
                Error(*field, "shape.thickness 必须大于 0（现在是 "
                                  + std::to_string(number) + "）");
                return false;
            }
            thickness = static_cast<float>(number);
            return true;
        };

        const std::string& name = type->text;
        if (name == "rect") {
            out = inking::ShapeSpec::Rect();
        } else if (name == "roundedRect") {
            const JsonValue* radius = value.Find("radius");
            if (radius == nullptr) {
                Error(*type, "roundedRect 需要 radius（设计坐标，不能为负）");
                return;
            }
            const double number = ReadNumber(*radius, "shape.radius");
            if (number < 0.0) {
                Error(*radius, "shape.radius 不能为负（现在是 "
                                   + std::to_string(number) + "）");
                return;
            }
            // 上限由形状自己夹（min(w,h)/2），这是数学要求不是配置错误。
            out = inking::ShapeSpec::RoundedRect(static_cast<float>(number));
        } else if (name == "circle") {
            out = inking::ShapeSpec::Circle();
        } else if (name == "ellipse") {
            out = inking::ShapeSpec::Ellipse();
        } else if (name == "capsule") {
            // 胶囊不需要参数：两端半圆的半径由 min(w,h)/2 定。
            out = inking::ShapeSpec::Capsule();
        } else if (name == "ring") {
            float thickness = 0.0f;
            if (!readThickness("ring", thickness)) {
                return;
            }
            out = inking::ShapeSpec::Ring(thickness);
        } else if (name == "line") {
            float thickness = 0.0f;
            if (!readThickness("line", thickness)) {
                return;
            }
            // cap 缺省是平头（和 CSS 的 stroke-linecap 默认值一致）。
            inking::LineCap cap = inking::LineCap::Butt;
            if (const JsonValue* field = value.Find("cap")) {
                if (!field->IsString()) {
                    Error(*field,
                          "shape.cap 必须是字符串（butt / round / square）");
                    return;
                }
                if (field->text == "butt") {
                    cap = inking::LineCap::Butt;
                } else if (field->text == "round") {
                    cap = inking::LineCap::Round;
                } else if (field->text == "square") {
                    cap = inking::LineCap::Square;
                } else {
                    Error(*field, "shape.cap \"" + field->text
                                      + "\" 不认识（只支持 butt、round、"
                                        "square）");
                    return;
                }
            }
            out = inking::ShapeSpec::Line(thickness, cap);
        } else if (name == "polygon") {
            const JsonValue* field = value.Find("sides");
            if (field == nullptr) {
                Error(*type, "polygon 需要 sides（边数，>= 3）");
                return;
            }
            const double sides = ReadNumber(*field, "shape.sides");
            if (sides < 3.0) {
                Error(*field, "shape.sides 必须 >= 3（现在是 "
                                  + std::to_string(sides) + "）");
                return;
            }
            if (std::fabs(sides - std::floor(sides + 0.5)) > 1e-9) {
                Error(*field, "shape.sides 必须是整数（现在是 "
                                  + std::to_string(sides) + "）");
                return;
            }
            // rotation 是**起始角**，单位是度——跟 transform.rotate 统一
            // （InkingTransform.h 定的那条）。不写就是 0（第一个顶点在 +x）。
            float rotation = 0.0f;
            if (const JsonValue* rotationField = value.Find("rotation")) {
                rotation = static_cast<float>(
                    ReadNumber(*rotationField, "shape.rotation"));
            }
            out = inking::ShapeSpec::Polygon(static_cast<int>(sides + 0.5),
                                          rotation);
        } else if (name == "chamferRect") {
            const JsonValue* field = value.Find("chamfer");
            if (field == nullptr) {
                Error(*type, "chamferRect 需要 chamfer（切角长度，不能为负）");
                return;
            }
            const double chamfer = ReadNumber(*field, "shape.chamfer");
            if (chamfer < 0.0) {
                Error(*field, "shape.chamfer 不能为负（现在是 "
                                  + std::to_string(chamfer) + "）");
                return;
            }
            // 切角过长会吃掉整条边 —— 上限由形状自己夹（短边的一半）。
            out = inking::ShapeSpec::ChamferRect(static_cast<float>(chamfer));
        } else if (name == "star") {
            const JsonValue* field = value.Find("points");
            if (field == nullptr) {
                Error(*type, "star 需要 points（角数，>= 3）");
                return;
            }
            const double points = ReadNumber(*field, "shape.points");
            if (points < 3.0) {
                Error(*field, "shape.points 必须 >= 3（现在是 "
                                  + std::to_string(points) + "）");
                return;
            }
            if (std::fabs(points - std::floor(points + 0.5)) > 1e-9) {
                Error(*field, "shape.points 必须是整数（现在是 "
                                  + std::to_string(points) + "）");
                return;
            }
            // 内 / 外半径之比：缺省 0.5（"常见的五角星"那个比例）。
            double innerRatio = 0.5;
            if (const JsonValue* ratioField = value.Find("innerRatio")) {
                innerRatio = ReadNumber(*ratioField, "shape.innerRatio");
                if (innerRatio < 0.0 || innerRatio > 1.0) {
                    Error(*ratioField,
                          "shape.innerRatio 要在 0~1 之间（现在是 "
                              + std::to_string(innerRatio) + "）");
                    return;
                }
            }
            float rotation = 0.0f;
            if (const JsonValue* rotationField = value.Find("rotation")) {
                rotation = static_cast<float>(
                    ReadNumber(*rotationField, "shape.rotation"));
            }
            out = inking::ShapeSpec::Star(static_cast<int>(points + 0.5),
                                       static_cast<float>(innerRatio), rotation);
        } else if (name == "superellipse") {
            const JsonValue* field = value.Find("exponent");
            if (field == nullptr) {
                Error(*type, "superellipse 需要 exponent（指数，大于 0；"
                             "2 是椭圆，越大越接近矩形）");
                return;
            }
            const double exponent = ReadNumber(*field, "shape.exponent");
            if (exponent <= 0.0) {
                Error(*field, "shape.exponent 必须大于 0（现在是 "
                                  + std::to_string(exponent) + "）");
                return;
            }
            // 过大的指数会让 pow 溢出，上限由形状自己夹。
            out = inking::ShapeSpec::Superellipse(static_cast<float>(exponent));
        } else if (name == "arc") {
            float thickness = 0.0f;
            if (!readThickness("arc", thickness)) {
                return;
            }
            // 扫角必填：不写就没法知道画哪一段（整圈该写 ring）。
            const JsonValue* sweepField = value.Find("sweepAngle");
            if (sweepField == nullptr) {
                Error(*type, "arc 需要 sweepAngle（扫过多少度；整圈请用 ring）");
                return;
            }
            const double sweep = ReadNumber(*sweepField, "shape.sweepAngle");
            if (std::fabs(sweep) <= 0.0 || std::fabs(sweep) > 360.0) {
                Error(*sweepField,
                      "shape.sweepAngle 要在 -360~360 之间且不能为 0（现在是 "
                          + std::to_string(sweep) + "）");
                return;
            }
            // 起始角可选，缺省 0（正右方）。单位度、顺时针为正。
            float startAngle = 0.0f;
            if (const JsonValue* startField = value.Find("startAngle")) {
                startAngle = static_cast<float>(
                    ReadNumber(*startField, "shape.startAngle"));
            }
            out = inking::ShapeSpec::Arc(thickness, startAngle,
                                      static_cast<float>(sweep));
        } else if (name == "polygonPath") {
            if (pathPoints == nullptr) {
                Error(*type, "shape.type \"polygonPath\" 面板还不支持"
                             "（面板是容器，形状只用来画它自己那一块）");
                return;
            }
            const JsonValue* field = value.Find("vertices");
            if (field == nullptr || !field->IsArray()) {
                Error(value, "polygonPath 需要 vertices（顶点表 "
                             "[x0,y0,x1,y1,…]，设计坐标，至少 3 个点）");
                return;
            }
            if (field->items.size() < 6 || field->items.size() % 2 != 0) {
                Error(*field,
                      "polygonPath.vertices 必须是**偶数个**数字、至少 6 个"
                      "（3 个点 x,y）——现在有 "
                          + std::to_string(field->items.size()) + " 个");
                return;
            }
            std::vector<float> points;
            points.reserve(field->items.size());
            for (const JsonValue& item : field->items) {
                points.push_back(
                    static_cast<float>(ReadNumber(item, "shape.vertices")));
            }
            // 点列交给调用方持有（`ShapeSpec` 里只有视图），`Emit` 会把这份数据
            // 烘成生成代码里的静态数组。
            *pathPoints = std::move(points);
            out = inking::ShapeSpec::PolygonPath(
                pathPoints->data(), static_cast<int>(pathPoints->size() / 2));
        } else {
            Error(*type, "shape.type \"" + name
                             + "\" 不认识（现在支持：rect、roundedRect、"
                               "circle、ellipse、capsule、ring、line、"
                               "polygon、chamferRect、star、superellipse、arc）");
        }
    }

    /**
     * 描边（B3）：`{ "width": 3, "color": "0|0|0|0.8" }`。
     *
     * 与 `shape` **平级**（`CXXCSS.md` §3.5）：形状只管几何，描边是画在边界上的
     * 另一笔。所以"描边宽度"不写进 `shape` 的参数里——那会让同一个圆角矩形
     * 出两套半径，而且宽度一变，形状的身份（缓存键、命中包络）就跟着变。
     */
    void ReadStroke(const JsonValue& value, inking::StrokeSpec& out) {
        if (!value.IsObject()) {
            Error(value, "stroke 必须是对象，形如 "
                         "{\"width\":3,\"color\":\"0|0|0|0.8\"}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (!IsKnownField(kKnownStrokeFields,
                              std::size(kKnownStrokeFields), entry.first)) {
                Error(entry.second,
                      "stroke 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 "
                          + JoinKnownFields(kKnownStrokeFields,
                                            std::size(kKnownStrokeFields))
                          + "）");
            }
        }

        const JsonValue* width = value.Find("width");
        if (width == nullptr) {
            Error(value, "stroke 需要 width（描边宽度，设计坐标、必须大于 0）");
            return;
        }
        const double number = ReadNumber(*width, "stroke.width");
        if (number <= 0.0) {
            Error(*width, "stroke.width 必须大于 0（现在是 "
                              + std::to_string(number) + "）");
            return;
        }
        out.width = static_cast<float>(number);

        // 颜色可选：不写就是黑（和 SVG 的默认描边色一致）。
        if (const JsonValue* color = value.Find("color")) {
            out.color = ReadColorSource(*color, "stroke.color");
        }
    }

    /**
     * 阴影（B3）：`{ "offsetX": 0, "offsetY": 4, "color": "0|0|0|0.35" }`。
     *
     * 现在画的是**偏移的实心副本**（没有模糊）——模糊要离屏目标 + 卷积，
     * 那是另一条渲染路径。所以这里**不认 blur 字段**：写它会明确报错，
     * 而不是收下之后悄悄不生效。
     */
    void ReadShadow(const JsonValue& value, inking::ShadowSpec& out) {
        if (!value.IsObject()) {
            Error(value, "shadow 必须是对象，形如 "
                         "{\"offsetX\":0,\"offsetY\":4,\"color\":\"0|0|0|0.35\"}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (!IsKnownField(kKnownShadowFields,
                              std::size(kKnownShadowFields), entry.first)) {
                Error(entry.second,
                      "shadow 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 "
                          + JoinKnownFields(kKnownShadowFields,
                                            std::size(kKnownShadowFields))
                          + "；模糊还没做，所以没有 blur）");
            }
        }

        if (const JsonValue* offsetX = value.Find("offsetX")) {
            out.offsetX = static_cast<float>(ReadNumber(*offsetX, "shadow.offsetX"));
        }
        if (const JsonValue* offsetY = value.Find("offsetY")) {
            out.offsetY = static_cast<float>(ReadNumber(*offsetY, "shadow.offsetY"));
        }
        if (const JsonValue* color = value.Find("color")) {
            out.color = ReadColorSource(*color, "shadow.color");
        }
    }

    /**
     * 读 `padding` / `margin` 这类"四边间距"（A6）。
     *
     * 两种写法都认：**一个数字**（四边相同，最常用）或**一个对象**
     * （`left` / `top` / `right` / `bottom`，缺的按 0）。写成负数直接报错——
     * 负间距在布局里没有定义清楚的语义，与其猜不如拒绝。
     */
    void ReadSpacing(const JsonValue& value, const std::string& field,
                     inking::LayoutSpacing& out) {
        if (value.IsNumber()) {
            const float all = static_cast<float>(ReadNumber(value, field));
            if (all < 0.0f) {
                Error(value, field + " 不能是负数");
                return;
            }
            out = inking::LayoutSpacing{all, all, all, all};
            return;
        }
        if (!value.IsObject()) {
            Error(value, field + " 要么是一个数字（四边相同），"
                                "要么是 {\"left\":..,\"top\":..,\"right\":..,"
                                "\"bottom\":..}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (entry.first != "left" && entry.first != "top"
                && entry.first != "right" && entry.first != "bottom") {
                Error(entry.second,
                      field + " 里的字段 \"" + entry.first
                          + "\" 不认识（只认 left / top / right / bottom）");
            }
        }
        const auto readSide = [&](const char* key, float& target) {
            if (const JsonValue* side = value.Find(key)) {
                const double number = ReadNumber(*side, field + "." + key);
                if (number < 0.0) {
                    Error(*side, field + "." + key + " 不能是负数");
                    return;
                }
                target = static_cast<float>(number);
            }
        };
        readSide("left", out.left);
        readSide("top", out.top);
        readSide("right", out.right);
        readSide("bottom", out.bottom);
    }

    /**
     * @brief 读字体字段：字符串，或**字符串数组**（0.3.0 T-13）。
     *
     * 数组 = 回退链：**第一个元素进主字段**（`ButtonTextData::path` /
     * `TextStyle::fontPath`），其余进链尾。这样"首选"仍然只有一个字段
     * （构造日志只认主字段），链只是它的尾巴。
     *
     * 两种写法都不会被静默吞掉：
     *   - 数组里有非字符串 → 报错并指到那一项（不是"整段配置读不下去"）；
     *   - 数组里有空串 → 报错（`""` 的语义是"用机器默认字体"，混在链里
     *     等于"退到一半突然改用默认字体"，那不是回退，是两套语义叠在一起）；
     *   - `[]` 不报错：它就是"没写字体"，与 `""` 同义。
     */
    void ReadFontPaths(const JsonValue& child, const char* fieldName,
                       std::string* primary,
                       std::vector<std::string>* fallbacks) {
        fallbacks->clear();
        if (child.IsString()) {
            *primary = child.text;
            return;
        }
        if (!child.IsArray()) {
            Error(child, std::string(fieldName)
                             + " 必须是字符串或字符串数组（字体绝对路径；"
                               "字符串数组 = 回退链，第一个元素优先；"
                               "空串 / 空数组 = 机器默认字体）");
            return;
        }
        if (child.items.empty()) {
            primary->clear();
            return;
        }
        for (std::size_t index = 0; index < child.items.size(); ++index) {
            const JsonValue& item = child.items[index];
            const std::string ordinal =
                std::string(fieldName) + " 的第 " + std::to_string(index + 1)
                + " 项";
            if (!item.IsString()) {
                Error(item, ordinal + " 必须是字符串");
                return;
            }
            if (item.text.empty()) {
                Error(item, ordinal + " 是空串（要机器默认字体就别写 "
                                + fieldName + "；回退链里不放空串）");
                return;
            }
            if (index == 0) {
                *primary = item.text;
            } else {
                fallbacks->push_back(item.text);
            }
        }
    }

    /// 读一个枚举字段：认表里的字符串，其它一律报错并列出可选项。
    bool ReadEnum(const JsonValue& value, const std::string& field,
                  const std::vector<std::pair<std::string, int>>& table,
                  int& out) {
        if (!value.IsString()) {
            Error(value, field + " 必须是字符串（" + JoinEnumNames(table) + "）");
            return false;
        }
        for (const auto& entry : table) {
            if (entry.first == value.text) {
                out = entry.second;
                return true;
            }
        }
        Error(value, "认不出 " + field + " = \"" + value.text + "\"（只认 "
                         + JoinEnumNames(table) + "）");
        return false;
    }

    static std::string JoinEnumNames(
        const std::vector<std::pair<std::string, int>>& table) {
        std::string joined;
        for (const auto& entry : table) {
            if (!joined.empty()) {
                joined += " / ";
            }
            joined += entry.first;
        }
        return joined;
    }

    /**
     * 读容器的布局规则（A6）：`"layout": { "kind": "stack", ... }`。
     *
     * 规则住在**容器**上；孩子那边写的是 `layoutItem`（见下）。字段名与
     * `include/ink/dataStruct/InkingLayout.h` 一一对应，`CXXCSS.md` §3.7 有表。
     */
    /**
     * B12：容器的滚动配置（`"scroll": {...}`）。
     *
     * 按用户裁定的范围，这一版只有三样东西：**轴**（`axes`）、**滚轮步长**
     * （`wheelStep`，固定设计像素——没有"视口比例"那一档）、**条的显示策略与几何**。
     * 没有缓动开关（滚轮与点轨道都瞬时到位），也没有惯性 / 回弹。
     */
    void ReadScroll(const JsonValue& value, inking::ScrollSpec& out) {
        if (!value.IsObject()) {
            Error(value, "scroll 必须是对象，形如 "
                         "{\"axes\":\"vertical\",\"wheelStep\":48}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (entry.first != "axes" && entry.first != "wheelStep"
                && entry.first != "verticalBar"
                && entry.first != "horizontalBar" && entry.first != "barWidth"
                && entry.first != "barInset"
                && entry.first != "minThumbLength"
                && entry.first != "barRadius") {
                Error(entry.second,
                      "scroll 里的字段 \"" + entry.first
                          + "\" 不认识（只认 axes / wheelStep / verticalBar / "
                            "horizontalBar / barWidth / barInset / minThumbLength）");
            }
        }

        out.enabled = true;
        if (const JsonValue* axes = value.Find("axes")) {
            int parsed = 0;
            if (ReadEnum(*axes, "scroll.axes",
                         {{"vertical", 0}, {"horizontal", 1}, {"both", 2}},
                         parsed)) {
                out.vertical = parsed != 1;
                out.horizontal = parsed != 0;
            }
        }
        if (const JsonValue* step = value.Find("wheelStep")) {
            out.wheelStep =
                static_cast<float>(ReadNumber(*step, "scroll.wheelStep"));
        }
        // 两条轴的显示策略是同一套枚举，分开写两遍比抽一层更直白。
        if (const JsonValue* bar = value.Find("verticalBar")) {
            int parsed = 0;
            if (ReadEnum(*bar, "scroll.verticalBar",
                         {{"auto", 0}, {"always", 1}, {"never", 2}}, parsed)) {
                out.verticalBar = parsed == 0   ? inking::ScrollAxisMode::Auto
                                  : parsed == 1 ? inking::ScrollAxisMode::Always
                                                : inking::ScrollAxisMode::Never;
            }
        }
        if (const JsonValue* bar = value.Find("horizontalBar")) {
            int parsed = 0;
            if (ReadEnum(*bar, "scroll.horizontalBar",
                         {{"auto", 0}, {"always", 1}, {"never", 2}}, parsed)) {
                out.horizontalBar = parsed == 0   ? inking::ScrollAxisMode::Auto
                                    : parsed == 1 ? inking::ScrollAxisMode::Always
                                                  : inking::ScrollAxisMode::Never;
            }
        }
        if (const JsonValue* width = value.Find("barWidth")) {
            out.barWidth =
                static_cast<float>(ReadNumber(*width, "scroll.barWidth"));
        }
        if (const JsonValue* inset = value.Find("barInset")) {
            out.barInset =
                static_cast<float>(ReadNumber(*inset, "scroll.barInset"));
        }
        if (const JsonValue* minThumb = value.Find("minThumbLength")) {
            out.minThumbLength = static_cast<float>(
                ReadNumber(*minThumb, "scroll.minThumbLength"));
        }
        if (const JsonValue* radius = value.Find("barRadius")) {
            out.barRadius =
                static_cast<float>(ReadNumber(*radius, "scroll.barRadius"));
        }
    }

    void ReadLayout(const JsonValue& value, inking::LayoutSpec& out) {
        if (!value.IsObject()) {
            Error(value, "layout 必须是对象，形如 "
                         "{\"kind\":\"stack\",\"gap\":12,\"align\":\"stretch\"}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (entry.first != "kind" && entry.first != "axis"
                && entry.first != "gap" && entry.first != "padding"
                && entry.first != "justify" && entry.first != "align"
                && entry.first != "columns" && entry.first != "rowGap"
                && entry.first != "columnGap") {
                Error(entry.second,
                      "layout 里的字段 \"" + entry.first
                          + "\" 不认识（只认 kind / axis / gap / padding / "
                            "justify / align / columns / rowGap / columnGap）");
            }
        }

        if (const JsonValue* kind = value.Find("kind")) {
            int parsed = 0;
            if (ReadEnum(*kind, "layout.kind",
                         {{"stack", 1}, {"grid", 2}}, parsed)) {
                out.kind = parsed == 1 ? inking::LayoutKind::Stack
                                       : inking::LayoutKind::Grid;
            }
        }
        if (const JsonValue* axis = value.Find("axis")) {
            int parsed = 0;
            if (ReadEnum(*axis, "layout.axis",
                         {{"vertical", 0}, {"horizontal", 1}}, parsed)) {
                out.axis = parsed == 0 ? inking::LayoutAxis::Vertical
                                       : inking::LayoutAxis::Horizontal;
            }
        }
        if (const JsonValue* gap = value.Find("gap")) {
            out.gap = static_cast<float>(ReadNumber(*gap, "layout.gap"));
        }
        if (const JsonValue* padding = value.Find("padding")) {
            ReadSpacing(*padding, "layout.padding", out.padding);
        }
        if (const JsonValue* justify = value.Find("justify")) {
            int parsed = 0;
            if (ReadEnum(*justify, "layout.justify",
                         {{"start", 0},
                          {"center", 1},
                          {"end", 2},
                          {"spaceBetween", 3},
                          {"spaceAround", 4},
                          {"spaceEvenly", 5}},
                         parsed)) {
                out.justify = static_cast<inking::LayoutJustify>(parsed);
            }
        }
        if (const JsonValue* align = value.Find("align")) {
            int parsed = 0;
            if (ReadEnum(*align, "layout.align",
                         {{"start", 0}, {"center", 1}, {"end", 2},
                          {"stretch", 3}},
                         parsed)) {
                out.align = static_cast<inking::LayoutAlign>(parsed);
            }
        }
        if (const JsonValue* columns = value.Find("columns")) {
            const double number = ReadNumber(*columns, "layout.columns");
            if (number < 1.0) {
                Error(*columns, "layout.columns 至少是 1");
            } else {
                out.columns = static_cast<int>(number);
            }
        }
        if (const JsonValue* rowGap = value.Find("rowGap")) {
            out.rowGap = static_cast<float>(ReadNumber(*rowGap, "layout.rowGap"));
        }
        if (const JsonValue* columnGap = value.Find("columnGap")) {
            out.columnGap
                = static_cast<float>(ReadNumber(*columnGap, "layout.columnGap"));
        }

        if (out.kind == inking::LayoutKind::Grid && out.columns < 1) {
            Error(value, "layout 用 grid 时 columns 至少是 1");
            out.columns = 1;
        }
    }

    /// 读"我在父容器布局里的意愿"（A6）：`"layoutItem": {...}`。
    void ReadLayoutItem(const JsonValue& value, inking::LayoutItem& out) {
        if (!value.IsObject()) {
            Error(value, "layoutItem 必须是对象，形如 "
                         "{\"fillMain\":true,\"margin\":8}");
            return;
        }
        for (const auto& entry : value.fields) {
            if (entry.first != "margin" && entry.first != "fillMain"
                && entry.first != "minWidth" && entry.first != "minHeight"
                && entry.first != "maxWidth" && entry.first != "maxHeight"
                && entry.first != "selfAlign") {
                Error(entry.second,
                      "layoutItem 里的字段 \"" + entry.first
                          + "\" 不认识（只认 margin / fillMain / minWidth / "
                            "minHeight / maxWidth / maxHeight / selfAlign）");
            }
        }

        if (const JsonValue* margin = value.Find("margin")) {
            ReadSpacing(*margin, "layoutItem.margin", out.margin);
        }
        if (const JsonValue* fill = value.Find("fillMain")) {
            if (fill->IsBool()) {
                out.fillMain = fill->boolean;
            } else {
                Error(*fill, "layoutItem.fillMain 必须是 true 或 false");
            }
        }
        const auto readSize = [&](const char* key, float& target) {
            if (const JsonValue* size = value.Find(key)) {
                const double number = ReadNumber(*size, std::string("layoutItem.") + key);
                if (number < 0.0) {
                    Error(*size, std::string("layoutItem.") + key + " 不能是负数");
                    return;
                }
                target = static_cast<float>(number);
            }
        };
        readSize("minWidth", out.minWidth);
        readSize("minHeight", out.minHeight);
        readSize("maxWidth", out.maxWidth);
        readSize("maxHeight", out.maxHeight);

        if (const JsonValue* selfAlign = value.Find("selfAlign")) {
            int parsed = 0;
            if (ReadEnum(*selfAlign, "layoutItem.selfAlign",
                         {{"start", 0}, {"center", 1}, {"end", 2},
                          {"stretch", 3}},
                         parsed)) {
                out.hasSelfAlign = true;
                out.selfAlign = static_cast<inking::LayoutAlign>(parsed);
            }
        }
        if (out.minWidth > 0.0f && out.maxWidth > 0.0f
            && out.maxWidth < out.minWidth) {
            Error(value, "layoutItem 的 maxWidth 比 minWidth 还小");
        }
        if (out.minHeight > 0.0f && out.maxHeight > 0.0f
            && out.maxHeight < out.minHeight) {
            Error(value, "layoutItem 的 maxHeight 比 minHeight 还小");
        }
    }

    /**
     * name 的三条规矩：**必填**、**等于文件名**、**跨组件类型唯一**。全过返回 true。
     *
     * 为什么独立成一段：语言表（A7 的 `Text`）也要这三条，但它**没有尺寸**——
     * 塞进 `CheckNameAndSize` 就会被迫"假装写了 width"。
     *
     * 重名检查必须看得见**别的组件类型**认领过什么（名字是运行期的全局查找键）。
     * A2 加面板时踩过一次：那时重名检查挤在"name != 文件名"那个 else-if 里，
     * 靠"两个文件重名 ⇒ 至少一个对不上文件名"这条推断，只在**同一目录**下成立，
     * `Button/foo.json` + `Panel/foo.json` 就这么漏检了（下面那段原注就是它）。
     */
    bool CheckName(const std::string& name, bool sawNameField) {
        bool ok = true;

        // 只在配置**真的写了 name** 时查重名：没写 name 的情况下面那条"缺少 name"
        // 已经说明问题了，再报一次重名只是噪音。
        if (sawNameField) {
            std::string owner;
            for (const auto& claimed : _result.claimedNames) {
                if (claimed.first == name) {
                    owner = claimed.second;
                    break;
                }
            }
            if (!owner.empty()) {
                _result.errors.push_back(CxxcssError{
                    _path, 0,
                    "name \"" + name + "\" 已经被 " + owner
                        + " 用了（名字必须唯一——**跨组件类型也唯一**，"
                          "见 CXXCSS.md §1）"});
                ok = false;
            }
        }

        if (!sawNameField) {
            // 配置里根本没写 name。文件名虽然能兜出名字，但 CXXCSS.md §1 第 2 条
            // 要的是两处**都写着且一致**——缺一处就是缺一处，不能悄悄替它决定。
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "缺少 name（必须显式写出，且与文件名 \"" + FileStem(_path)
                    + "\" 一致）"});
            ok = false;
        } else if (name != FileStem(_path) && ok) {
            // 到这一步还能进来的，就是纯粹的"两处对不上"（重名已经在上面报过，
            // 同一处报两条更难读）。
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "name \"" + name + "\" 与文件名 \"" + FileStem(_path)
                    + "\" 不一致（生成器按名字生成类与查找键，"
                      "两处对不上就查不到，见 CXXCSS.md §1）"});
            ok = false;
        }

        if (!ok) {
            _failed = true;
        }
        return ok;
    }

    /**
     * 组件配置的收尾检查：上面那三条 + **width / height 必填**。全过返回 true。
     *
     * `requireHeight = false` 是给**标签**留的口子（A7）：标签的高度可以省略
     * （0 = 完全由内容撑高，`LabelData::height` 与 `MeasureLayout` 就是这么约定的）。
     * 那和"忘了写"不是一回事，不能一刀切要求必填；宽度则仍然必填——它是换行的依据。
     */
    bool CheckNameAndSize(const std::string& name, bool sawNameField,
                          bool sawWidth, bool sawHeight,
                          bool requireHeight = true) {
        bool ok = CheckName(name, sawNameField);

        if (!sawWidth) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 width（设计坐标，必须大于 0）"});
            ok = false;
        }
        if (requireHeight && !sawHeight) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 height（设计坐标，必须大于 0）"});
            ok = false;
        }

        if (!ok) {
            _failed = true;
        }
        return ok;
    }
};

/**
 * @brief 按钮的加载器：一次收齐所有错误，不早退。
 *
 * 早退会让用户"改一条、跑一次、又冒一条"；配置错误往往是一次写错一片，
 * 所以这里攒着一起报。
 *
 * 它直接往调用方给的 `LoadResult` 上追加（成功进 `buttons`、问题进 `errors`），
 * 不自己攒一份再合并——重名检查要看的是**这一批文件到目前为止**的认领情况，
 * 一份一份地"各自校验完再合并"就会漏掉"第二个文件撞上第一个"（踩过：
 * 那份实现里每个文件都只看得到自己，于是重名永远检不出来）。
 */
class ButtonLoader : public ConfigReader {
public:
    ButtonLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        // 先用文件名当默认值：配置里没写 name 时也还有话可说。
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        // `$extends` 的来源（T-10）：合并已经在 `LoadConfigInto` 里做完，
        // 这里只抄一份来源串给生成物注释用；字段本身由 `IsDirectiveField` 跳过。
        if (const JsonValue* extends = root.Find("$extends")) {
            if (extends->IsString()) {
                _config.extendsFrom = extends->text;
            }
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key)) {
                continue;  // CXXCSS.md §2.1：注释字段，生成器跳过
            }
            if (!IsKnownField(kKnownRootFields,
                              std::size(kKnownRootFields), key)) {
                Error(value, "字段 \"" + key + "\" 不认识（已知字段："
                                 + JoinKnownFields(kKnownRootFields,
                                                   std::size(kKnownRootFields))
                                 + "）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "width") {
                _config.data.width = ReadSize(value, "width");
                _sawWidth = true;
            } else if (key == "height") {
                _config.data.height = ReadSize(value, "height");
                _sawHeight = true;
            } else if (key == "normal") {
                ReadAppearance(value, "normal", _config.data.normal,
                               &_hasNormal, &_config.imageBytes[0]);
            } else if (key == "hover") {
                ReadAppearance(value, "hover", _config.data.hover, nullptr,
                               &_config.imageBytes[1]);
                _config.data.hoverInheritsNormal = false;
            } else if (key == "onclicked") {
                ReadAppearance(value, "onclicked", _config.data.onclicked,
                               nullptr, &_config.imageBytes[2]);
                _config.data.onclickedInheritsNormal = false;
            } else if (key == "text") {
                ReadText(value);
            } else if (key == "selfAnchorX") {
                ReadAnchor(value, "selfAnchorX", _config.data.selfAnchor.x);
            } else if (key == "selfAnchorY") {
                ReadAnchor(value, "selfAnchorY", _config.data.selfAnchor.y);
            } else if (key == "traceAnchorX") {
                ReadAnchor(value, "traceAnchorX", _config.data.traceAnchor.x);
            } else if (key == "traceAnchorY") {
                ReadAnchor(value, "traceAnchorY", _config.data.traceAnchor.y);
            } else if (key == "offsetX") {
                _config.data.offsetX = ReadNumber(value, "offsetX");
            } else if (key == "offsetY") {
                _config.data.offsetY = ReadNumber(value, "offsetY");
            } else if (key == "layout") {
                // A6：容器的布局规则（Button 与 Panel 用同一套读取器）。
                ReadLayout(value, _config.data.layout);
            } else if (key == "layoutItem") {
                // A6：这个组件在父容器布局里的意愿。
                ReadLayoutItem(value, _config.data.layoutItem);
            } else if (key == "zIndex") {
                _config.data.zIndex
                    = static_cast<int>(ReadNumber(value, "zIndex"));
            } else if (key == "visible") {
                if (value.IsBool()) {
                    _config.data.visible = value.boolean;
                } else {
                    Error(value, "visible 必须是 true 或 false");
                }
            } else if (key == "dynamic") {
                // 生成期的一个"选哪个基类"的开关，不进运行期数据结构。
                // 所以它只认 bool：写成字符串或数字都是笔误，
                // 而"悄悄按 truthy 处理"会让配置看着生效、其实没生效。
                if (value.IsBool()) {
                    _config.dynamic = value.boolean;
                } else {
                    Error(value, "dynamic 必须是 true 或 false（它决定生成的类"
                                     "继承 InkingStaticButton 还是 "
                                     "InkingDynamicButton）");
                }
            } else if (key == "shape") {
                _sawShape = true;
                // 按钮可以配 `polygonPath`：点列交给 ButtonConfig 持有。
                ReadShape(value, _config.data.shape, &_config.pathPoints);
            } else if (key == "stroke") {
                ReadStroke(value, _config.data.stroke);
            } else if (key == "shadow") {
                ReadShadow(value, _config.data.shadow);
            }
        }

        Finish();
    }

private:
    ButtonConfig _config;
    bool _hasNormal = false;
    bool _sawNameField = false;
    bool _sawWidth = false;
    bool _sawHeight = false;
    bool _sawShape = false;  ///< 配置里显式写了 shape（与 svg 几何核对用）

    /// svg 子集解析出来的原始描述（按状态存，几何校验在 Finish() 里做）。
    RawSvgShape _svg[3];
    bool _hasSvg[3] = {false, false, false};
    const JsonValue* _svgWhere[3] = {nullptr, nullptr, nullptr};

    /// 状态名 → 0/1/2（_svg / imageBytes 的下标）。不是三态返回 -1。
    static int StateIndexOf(const std::string& field) {
        if (field == "normal") return 0;
        if (field == "hover") return 1;
        if (field == "onclicked") return 2;
        return -1;
    }

    /**
     * 把图像文件读成字节（生成期做，T-4）。
     *
     * 两道闸：**读不到就报错**（以前是"收下并提醒"，现在这条路真的要走通了，
     * 悄悄不画是最坏的结果）；**超过上限也报错**（字节烘进源码会让编译时间
     * 线性上涨，`CXXCSS.md` §4.1 写了 2 MB 这条线）。
     */
    /// 读图片文件：本体是文件顶部那个自由函数（按钮与图片两条路共用）。
    bool ReadImageFile(const JsonValue& where, const std::string& path,
                       std::vector<std::uint8_t>& out) {
        const std::string problem = ReadImageBytes(path, out);
        if (!problem.empty()) {
            Error(where, problem);
            return false;
        }
        return true;
    }

    /**
     * svg 外观的几何会成为**这个组件的形状**（生成期转换，T-4）：
     *   - 所有 svg 状态必须给出**同一个几何**——形状不能随三态变，
     *     那是"静态档进命中表"的前提（T-5 的布局通道才谈几何变化）；
     *   - 配置里显式写了 `shape` 时，必须与 svg 几何一致，否则就回到
     *     "画的是一个、点的是另一个"那条老路。
     */
    void ApplySvgShapes() {
        int first = -1;
        for (int i = 0; i < 3; ++i) {
            if (_hasSvg[i]) {
                first = i;
                break;
            }
        }
        if (first < 0) {
            return;
        }

        inking::ShapeSpec shape;
        std::string error;
        if (!SvgToShape(_svg[first], _config.data.width, _config.data.height,
                        shape, error, &_config.pathPoints)) {
            Error(*_svgWhere[first], "normal/hover/onclicked 里的 svg " + error);
            return;
        }
        for (int i = first + 1; i < 3; ++i) {
            if (!_hasSvg[i]) {
                continue;
            }
            // `path` 的几何一致性要**比点列本身**：三个状态的 svg 会先后把点列
            // 写进同一个 `_config.pathPoints`，第二份会覆盖第一份——那时再比
            // `ShapeSpec` 里的视图，两边指向同一块缓冲，永远"一致"。
            if (_svg[first].tag == "path") {
                if (_svg[i].tag != "path"
                    || _svg[i].points != _svg[first].points) {
                    Error(*_svgWhere[i],
                          "三个状态的 svg 几何必须一致（形状不能随状态变——"
                          "那会破坏「静态档进命中表」这条前提）");
                    return;
                }
                continue;
            }
            inking::ShapeSpec other;
            std::string otherError;
            if (!SvgToShape(_svg[i], _config.data.width, _config.data.height,
                            other, otherError)) {
                Error(*_svgWhere[i],
                      "normal/hover/onclicked 里的 svg " + otherError);
                return;
            }
            if (!SameShape(shape, other)) {
                Error(*_svgWhere[i],
                      "三个状态的 svg 几何必须一致（形状不能随状态变——"
                      "那会破坏「静态档进命中表」这条前提）");
                return;
            }
        }
        if (_sawShape) {
            if (!SameShape(_config.data.shape, shape)) {
                Error(*_svgWhere[first],
                      "svg 的几何与 shape 字段对不上：svg 外观的几何**就是**"
                      "这个组件的形状，所以要么删掉 shape、要么让两者一致");
                return;
            }
        } else {
            _config.data.shape = shape;
        }
    }

    void ReadAppearance(const JsonValue& value, const std::string& field,
                        inking::ButtonAppearance& out, bool* sawNormal,
                        std::vector<std::uint8_t>* imageBytesOut) {
        if (!value.IsObject()) {
            Error(value, field + " 必须是对象，形如 "
                             "{\"type\":\"color\",\"source\":\"0|0|0|0.75\"}");
            return;
        }

        for (const auto& entry : value.fields) {
            if (!IsKnownField(kKnownButtonAppearanceFields,
                              std::size(kKnownButtonAppearanceFields),
                              entry.first)) {
                Error(entry.second,
                      field + " 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 "
                          + JoinKnownFields(kKnownButtonAppearanceFields,
                                            std::size(kKnownButtonAppearanceFields))
                          + "）");
            }
        }

        const JsonValue* type = value.Find("type");
        const JsonValue* source = value.Find("source");

        if (type == nullptr) {
            Error(value, field + " 缺少 type（color / img / svg）");
            return;
        }
        if (!type->IsString()) {
            Error(*type, field + ".type 必须是字符串");
            return;
        }

        std::string typeName = type->text;
        std::transform(typeName.begin(), typeName.end(), typeName.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });

        if (typeName == "color") {
            out.type = inking::AppearanceType::Color;
            if (source == nullptr) {
                Error(value, field + " 缺少 source（color 要写 \"r|g|b|a\"）");
                return;
            }
            out.color = ReadColorSource(*source, field, true, &out.themeColor);
            out.source = source->IsString() ? source->text : std::string();
        } else if (typeName == "img") {
            out.type = inking::AppearanceType::Image;
            if (source == nullptr || !source->IsString()) {
                Error(value, field + " 的 img 需要 source（图像绝对路径）");
                return;
            }
            out.source = source->text;
            // 图像在**生成期**读进来（`CXXCSS.md` §4.1 定的路）：编译期就发现
            // "文件没了 / 太大了"，而不是等运行期画出来一片空白。
            std::vector<std::uint8_t> scratch;
            std::vector<std::uint8_t>& bytes =
                imageBytesOut != nullptr ? *imageBytesOut : scratch;
            if (!ReadImageFile(*source, source->text, bytes)) {
                return;
            }
        } else if (typeName == "svg") {
            out.type = inking::AppearanceType::Svg;
            if (source == nullptr || !source->IsString()) {
                Error(value, field + " 的 svg 需要 source（SVG 代码）");
                return;
            }
            out.source = source->text;
            // svg 子集在生成期转成"形状 + 填充色"（TaskGuide T-4）：
            // 几何要等 width/height 读完才好校验，所以这里只解析出来存着，
            // 统一在 Finish() 里落成 data.shape。
            RawSvgShape raw;
            const std::string svgError = ParseSvgSubset(source->text, raw);
            if (!svgError.empty()) {
                Error(*source, field + ".source 的 svg 解析不了：" + svgError);
                return;
            }
            const int stateIndex = StateIndexOf(field);
            if (stateIndex >= 0) {
                _svg[stateIndex] = raw;
                _hasSvg[stateIndex] = true;
                _svgWhere[stateIndex] = source;
            }
            out.color = raw.fill;
        } else {
            Error(*type, field + ".type \"" + type->text
                             + "\" 不认识（只支持 color、img、svg）");
            return;
        }

        // 过渡写在状态自己身上（TaskGuide 的 D-1）：进了这个状态就用它的时长。
        // 外观本身有错（上面已经 return）时不再解析它——那条错更值得先修。
        if (const JsonValue* transition = value.Find("transition")) {
            ReadTransition(*transition, field, out.transitionSeconds,
                           out.easing);
        }

        // 变换同样是"这个状态自己带一份"：进这个状态就朝这份变换走。
        if (const JsonValue* transform = value.Find("transform")) {
            ReadTransform(*transform, field, out.transform);
        }

        // 布局通道（T-5）：这个状态自己的尺寸。写在状态里、由状态机切换，
        // 所以组件仍然是**静态档**（没有几何写入口）；尺寸从左上角生长，
        // 锚点与偏移不动（CXXCSS.md §4.1.3）。
        if (const JsonValue* widthField = value.Find("width")) {
            out.width =
                static_cast<float>(ReadSize(*widthField, field + ".width"));
        }
        if (const JsonValue* heightField = value.Find("height")) {
            out.height =
                static_cast<float>(ReadSize(*heightField, field + ".height"));
        }

        // 图片适配模式（B1）：只对 `img` 有意义，但**不**因为 type 是 color 就报错
        // ——三态外观常常一份抄一份，多写一个字段不值当红一次构建；写在 color 上
        // 就当没写（绘制那边只在画图片时读它）。
        if (const JsonValue* fitField = value.Find("fit")) {
            if (!fitField->IsString()) {
                Error(*fitField, field + ".fit 必须是字符串（fill / contain / cover）");
            } else {
                std::string fitName = fitField->text;
                std::transform(fitName.begin(), fitName.end(), fitName.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if (fitName == "fill") {
                    out.imageFit = inking::ImageFit::Fill;
                } else if (fitName == "contain") {
                    out.imageFit = inking::ImageFit::Contain;
                } else if (fitName == "cover") {
                    out.imageFit = inking::ImageFit::Cover;
                } else {
                    Error(*fitField, field + ".fit \"" + fitField->text
                                         + "\" 不认识（只支持 fill、contain、cover）");
                }
            }
        }

        if (sawNormal != nullptr) {
            *sawNormal = true;
        }
    }

    void ReadText(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "text 必须是对象");
            return;
        }
        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (!IsKnownField(kKnownTextFields, std::size(kKnownTextFields),
                              key)) {
                Error(child, "text 里的字段 \"" + key + "\" 不认识（已知："
                                 + JoinKnownFields(kKnownTextFields,
                                                   std::size(kKnownTextFields))
                                 + "）");
                continue;
            }

            if (key == "fontSize") {
                // 数字或语义字号（`"$body"`，B8）：两个组件类型共用同一个读取器。
                _config.data.text.fontSize = ReadFontSize(child, "text.fontSize");
            } else if (key == "leftSpace") {
                _config.data.text.leftSpace
                    = static_cast<float>(ReadNumber(child, "text.leftSpace"));
            } else if (key == "topSpace") {
                _config.data.text.topSpace
                    = static_cast<float>(ReadNumber(child, "text.topSpace"));
            } else if (key == "path") {
                ReadFontPaths(child, "text.path", &_config.data.text.path,
                              &_config.data.text.paths);
            } else if (key == "content") {
                ReadTextContent(child);
            } else if (key == "align") {
                // 文字块的水平摆法：默认 left（`leftSpace` 是距左距离）。
                if (!child.IsString()) {
                    Error(child, "text.align 必须是字符串：left / center / right");
                } else if (child.text == "left") {
                    _config.data.text.align = inking::TextAlignH::Left;
                } else if (child.text == "center") {
                    _config.data.text.align = inking::TextAlignH::Center;
                } else if (child.text == "right") {
                    _config.data.text.align = inking::TextAlignH::Right;
                } else {
                    Error(child, "text.align 只认 left / center / right，给的是 \""
                                     + child.text + "\"");
                }
            } else if (key == "alignV") {
                // 垂直摆法：默认 top（`topSpace` 是距顶距离）。
                if (!child.IsString()) {
                    Error(child,
                          "text.alignV 必须是字符串：top / middle / bottom");
                } else if (child.text == "top") {
                    _config.data.text.alignV = inking::TextAlignV::Top;
                } else if (child.text == "middle") {
                    _config.data.text.alignV = inking::TextAlignV::Middle;
                } else if (child.text == "bottom") {
                    _config.data.text.alignV = inking::TextAlignV::Bottom;
                } else {
                    Error(child,
                          "text.alignV 只认 top / middle / bottom，给的是 \""
                              + child.text + "\"");
                }
            }
        }
    }

    void ReadTextContent(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "text.content 必须是对象，形如 "
                         "{\"normal\":\"确定\"}");
            return;
        }
        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            if (key != "normal" && key != "hover" && key != "onClicked"
                && key != "onclicked") {
                Error(entry.second, "text.content 里的字段 \"" + key
                                        + "\" 不认识（只认 normal / hover / "
                                          "onClicked）");
                continue;
            }
            if (!entry.second.IsString()) {
                Error(entry.second,
                      "text.content." + key + " 必须是字符串");
                continue;
            }

            if (key == "normal") {
                // 三态文字里今天只有 normal 能画（hover / onClicked 要等换字形的
                // 通道）。看到另外两态与 normal 不同时提醒一句，别让人以为生效了。
                _config.data.label = entry.second.text;
            } else if (!_config.data.label.empty()
                       && entry.second.text != _config.data.label) {
                Warn("text.content." + key
                     + " 与 normal 不同，但运行时只画 normal 那一份"
                       "（换字形的通道还没做）");
            }
        }
    }

    /// 收尾：必填项（含共用的那几条）、重名、文件名与 name 的一致性。
    void Finish() {
        // name / 重名 / width / height 是两种组件共用的规矩——基类里只有一份实现，
        // 免得"面板放行了、按钮报错"这类分叉。
        CheckNameAndSize(_config.name, _sawNameField, _sawWidth, _sawHeight);

        if (!_hasNormal) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 normal（通常状态的外观）"});
            _failed = true;
        }

        // svg 外观的几何落成组件的形状（要等 width/height/normal 都读完）。
        // 前面已经出错时不用再报一遍——那种配置本来就不会生成。
        if (!_failed) {
            ApplySvgShapes();
        }

        _config.identifier = MakeNameConstant(_config.name);

        // 两处都要写：ButtonConfig::name 是"这份文件叫什么"（生成器自己用），
        // ButtonData::name 是运行时 ButtonLibrary 的查找键。生成器里漏掉后面
        // 这个，运行期按名字就查不到——而且库那边只会报"没登记过"。
        _config.data.name = _config.name;

        // 认领这个名字。**不过滤 _failed**：这份文件可能因为别的问题被拒绝、
        // 因而不进 buttons，但它认领了某个名字这件事仍然成立——下一个撞上它的
        // 文件必须看得见。顺序也有意如此：重名由**后到者**报出来。
        _result.claimedNames.push_back({_config.name, _path});

        // 非致命提醒由基类那份 `_warnings` 收着（`Warn()` 在 ConfigReader 里，
        // 两个 loader 走同一条路），这里搬进自己的 config。
        _config.warnings = std::move(_warnings);

        if (!_failed) {
            _result.buttons.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 面板（Panel）
// ---------------------------------------------------------------------------

/**
 * @brief 面板的加载器。
 *
 * 面板的配置比按钮少一截：**没有三态**（它是容器，不是控件——三态是指针状态，
 * 而开合是内容状态）、没有文字、没有 `img` / `svg` 外观，也没有"静态还是动态"
 * 的开关（面板只有静态档：开合只改变换，几何构造即定型）。
 *
 * 它有两个**具名状态**：`closed` 与 `open`，各带颜色 / 过渡 / 变换。开合由业务
 * 调 `SetOpen()` 决定，不是指针事件——所以配置里没有 hover / pressed。
 */
class PanelLoader : public ConfigReader {
public:
    PanelLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        // `$extends` 的来源（T-10）：合并已经在 `LoadConfigInto` 里做完，
        // 这里只抄一份来源串给生成物注释用；字段本身由 `IsDirectiveField` 跳过。
        if (const JsonValue* extends = root.Find("$extends")) {
            if (extends->IsString()) {
                _config.extendsFrom = extends->text;
            }
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key)) {
                continue;  // CXXCSS.md §2.1：注释字段，生成器跳过
            }
            if (!IsKnownField(kKnownPanelFields,
                              std::size(kKnownPanelFields), key)) {
                Error(value,
                      "字段 \"" + key + "\" 不认识（面板认的字段："
                          + JoinKnownFields(kKnownPanelFields,
                                            std::size(kKnownPanelFields))
                          + "）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "width") {
                _config.data.width = ReadSize(value, "width");
                _sawWidth = true;
            } else if (key == "height") {
                _config.data.height = ReadSize(value, "height");
                _sawHeight = true;
            } else if (key == "closed") {
                ReadAppearance(value, "closed", _config.data.closed);
                _hasClosed = true;
            } else if (key == "open") {
                ReadAppearance(value, "open", _config.data.open);
                // 显式写了 open 段就不再继承 closed 的颜色——和 ButtonData 的
                // hoverInheritsNormal 同一条规矩（AGENTS §6 第 43 条）。
                _config.data.openInheritsClosed = false;
            } else if (key == "startOpen") {
                if (value.IsBool()) {
                    _config.data.startOpen = value.boolean;
                } else {
                    Error(value, "startOpen 必须是 true 或 false");
                }
            } else if (key == "clipChildren") {
                // A5：剪掉子孙超出本体的部分。默认 false——面板也常用来装
                // 故意溢出的东西（阴影 / tooltip），默认打开会"悄悄少画一块"。
                if (value.IsBool()) {
                    _config.data.clipChildren = value.boolean;
                } else {
                    Error(value, "clipChildren 必须是 true 或 false");
                }
            } else if (key == "scroll") {
                // B12：滚动容器（竖直 / 水平 / 双向 + 条什么时候显示 + 几何）。
                ReadScroll(value, _config.data.scroll);
            } else if (key == "selfAnchorX") {
                ReadAnchor(value, "selfAnchorX", _config.data.selfAnchor.x);
            } else if (key == "selfAnchorY") {
                ReadAnchor(value, "selfAnchorY", _config.data.selfAnchor.y);
            } else if (key == "traceAnchorX") {
                ReadAnchor(value, "traceAnchorX", _config.data.traceAnchor.x);
            } else if (key == "traceAnchorY") {
                ReadAnchor(value, "traceAnchorY", _config.data.traceAnchor.y);
            } else if (key == "offsetX") {
                _config.data.offsetX = ReadNumber(value, "offsetX");
            } else if (key == "offsetY") {
                _config.data.offsetY = ReadNumber(value, "offsetY");
            } else if (key == "layout") {
                // A6：容器的布局规则（Button 与 Panel 用同一套读取器）。
                ReadLayout(value, _config.data.layout);
            } else if (key == "layoutItem") {
                // A6：这个组件在父容器布局里的意愿。
                ReadLayoutItem(value, _config.data.layoutItem);
            } else if (key == "zIndex") {
                _config.data.zIndex
                    = static_cast<int>(ReadNumber(value, "zIndex"));
            } else if (key == "visible") {
                if (value.IsBool()) {
                    _config.data.visible = value.boolean;
                } else {
                    Error(value, "visible 必须是 true 或 false");
                }
            } else if (key == "shape") {
                ReadShape(value, _config.data.shape);
            }
        }

        Finish();
    }

private:
    PanelConfig _config;
    bool _sawNameField = false;
    bool _sawWidth = false;
    bool _sawHeight = false;
    bool _hasClosed = false;  ///< 配置里写了 closed（必填）

    /**
     * 一个开合状态的外观（`closed` / `open`）。
     *
     * 与按钮的 `ReadAppearance` 同构，但**只认 `color`**：`PanelAppearance` 里
     * 没有"外观类型"这一维（面板不贴图、不开洞——它是容器）。写了 `img` / `svg`
     * 时明确报"面板还不支持这个"，而不是假装没看见。
     *
     * 0.2.0 T-07：状态里多收 `stroke` / `shadow` 两段（与按钮同两个读取器）。
     * 描边画在**子孙之上**（装饰遍），阴影在填充之下；两者都不参与命中。
     */
    void ReadAppearance(const JsonValue& value, const std::string& field,
                        inking::PanelAppearance& out) {
        if (!value.IsObject()) {
            Error(value,
                  field
                      + " 必须是对象，形如 {\"type\":\"color\","
                        "\"source\":\"0.1|0.1|0.12|1\","
                        "\"transition\":{\"duration\":0.3}}");
            return;
        }

        for (const auto& entry : value.fields) {
            if (!IsKnownField(kKnownPanelAppearanceFields,
                              std::size(kKnownPanelAppearanceFields),
                              entry.first)) {
                Error(entry.second,
                      field + " 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 "
                          + JoinKnownFields(kKnownPanelAppearanceFields,
                                            std::size(kKnownPanelAppearanceFields))
                          + "）");
            }
        }

        const JsonValue* type = value.Find("type");
        const JsonValue* source = value.Find("source");
        if (type == nullptr) {
            Error(value, field + " 缺少 type（面板现在只支持 color）");
            return;
        }
        if (!type->IsString()) {
            Error(*type, field + ".type 必须是字符串");
            return;
        }
        if (type->text != "color") {
            Error(*type, field + ".type \"" + type->text
                                 + "\" 面板外观还不支持（现在只有 color；"
                                   "img / svg 是按钮那条路的东西）");
            return;
        }
        if (source == nullptr) {
            Error(value, field + " 缺少 source（color 要写 \"r|g|b|a\"）");
            return;
        }
        out.color = ReadColorSource(*source, field, true, &out.themeColor);

        // 过渡与变换：与按钮同两个读取器（"进这个状态用多久 / 朝哪儿走"）。
        if (const JsonValue* transition = value.Find("transition")) {
            ReadTransition(*transition, field, out.transitionSeconds,
                           out.easing);
        }
        if (const JsonValue* transform = value.Find("transform")) {
            ReadTransform(*transform, field, out.transform);
        }
        // 描边与阴影（T-07）：与按钮同两个读取器。`open` 省略时由
        // `PanelData::Normalize` 连同颜色一起继承 `closed` 那份。
        if (const JsonValue* stroke = value.Find("stroke")) {
            ReadStroke(*stroke, out.stroke);
        }
        if (const JsonValue* shadow = value.Find("shadow")) {
            ReadShadow(*shadow, out.shadow);
        }
    }

    /// 收尾：必填项（含共用的那几条）、重名、文件名与 name 的一致性。
    void Finish() {
        CheckNameAndSize(_config.name, _sawNameField, _sawWidth, _sawHeight);

        if (!_hasClosed) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 closed（关着时的外观）"});
            _failed = true;
        }

        // 面板的 name **必须**是合法 C++ 标识符：按钮那边名字不合法还能走
        // "按字符串名字查"（数据进 ButtonLibrary 兜着），而面板没有库——
        // 没有类就等于没有任何用法。所以这里报错，而不是生成一个用不了的东西。
        if (!_failed && !IsCxxIdentifier(_config.name)) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "面板的 name \"" + _config.name
                    + "\" 不是合法的 C++ 标识符（只能字母数字下划线、不以数字"
                      "开头）。按钮允许这种名字——它还有「按字符串名字查」那条路；"
                      "面板没有库可查，名字不合法就没有任何用法"});
            _failed = true;
        }

        _config.identifier = MakeNameConstant(_config.name);
        _config.data.name = _config.name;
        _config.warnings = std::move(_warnings);

        // 认领名字：**与按钮共用同一份 claimedNames**——一个按钮和一个面板叫
        // 同一个名字同样是错（名字是运行期的全局查找键）。
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            _result.panels.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 标签（Label，A7 的生成器段）
// ---------------------------------------------------------------------------

/**
 * @brief 标签的加载器。
 *
 * 字段各归各的落点（对应 `LabelData` 的三块）：
 *   - 尺寸 / 锚点 / 层级 / 布局 → 组件那一块，与别的组件同义；
 *   - `text.content` → **文字内容**（占位符或字面量，运行期由 `ResolveText` 查表）；
 *   - `text` 段其余字段 → **排版样式**（`TextStyle`）+ 换行 / 省略开关。
 *
 * `width` 必填（换行的依据）、`height` 可省（0 = 完全由内容撑高）。
 * 名字**必须是合法标识符**：标签和面板一样没有"按名字查的库"，名字不合法就
 * 没有任何用法（`InkingStaticLabel` 只有数据构造那条路）。
 */
class LabelLoader : public ConfigReader {
public:
    LabelLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        // `$extends` 的来源（T-10）：合并已经在 `LoadConfigInto` 里做完，
        // 这里只抄一份来源串给生成物注释用；字段本身由 `IsDirectiveField` 跳过。
        if (const JsonValue* extends = root.Find("$extends")) {
            if (extends->IsString()) {
                _config.extendsFrom = extends->text;
            }
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key)) {
                continue;  // CXXCSS.md §2.1：注释字段，生成器跳过
            }
            if (!IsKnownField(kKnownLabelFields,
                              std::size(kKnownLabelFields), key)) {
                Error(value, "字段 \"" + key + "\" 不认识（标签认的字段："
                                 + JoinKnownFields(kKnownLabelFields,
                                                   std::size(kKnownLabelFields))
                                 + "；标签不画底，所以没有 shape / stroke / shadow）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "width") {
                _config.data.width = ReadSize(value, "width");
                _sawWidth = true;
            } else if (key == "height") {
                // 可省：0 就是"高度由内容撑"（A6 的 MeasureLayout 报内容尺寸）。
                // **写 0 和省略等价**——文档一直这么写，所以这里必须收 0；
                // 以前 `ReadSize` 一刀切要 > 0，照文档写的人会在构建期被拒
                // （外部使用方就踩了这条：文档说"省略 = 0"，他写了 0）。
                _config.data.height =
                    ReadSize(value, "height", /*allowZero=*/true);
                _sawHeight = true;
            } else if (key == "text") {
                ReadTextSegment(value);
            } else if (key == "selfAnchorX") {
                ReadAnchor(value, "selfAnchorX", _config.data.selfAnchor.x);
            } else if (key == "selfAnchorY") {
                ReadAnchor(value, "selfAnchorY", _config.data.selfAnchor.y);
            } else if (key == "traceAnchorX") {
                ReadAnchor(value, "traceAnchorX", _config.data.traceAnchor.x);
            } else if (key == "traceAnchorY") {
                ReadAnchor(value, "traceAnchorY", _config.data.traceAnchor.y);
            } else if (key == "offsetX") {
                _config.data.offsetX = ReadNumber(value, "offsetX");
            } else if (key == "offsetY") {
                _config.data.offsetY = ReadNumber(value, "offsetY");
            } else if (key == "layout") {
                ReadLayout(value, _config.data.layout);
            } else if (key == "layoutItem") {
                ReadLayoutItem(value, _config.data.layoutItem);
            } else if (key == "zIndex") {
                _config.data.zIndex
                    = static_cast<int>(ReadNumber(value, "zIndex"));
            } else if (key == "visible") {
                if (value.IsBool()) {
                    _config.data.visible = value.boolean;
                } else {
                    Error(value, "visible 必须是 true 或 false");
                }
            }
        }

        Finish();
    }

private:
    LabelConfig _config;
    bool _sawNameField = false;
    bool _sawWidth = false;
    bool _sawHeight = false;
    bool _sawText = false;

    /// 标签的 `text` 段：内容 + 排版样式 + 换行 / 省略。
    void ReadTextSegment(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "text 必须是对象，形如 {\"content\":\"menu.title\","
                         "\"fontSize\":24,\"align\":\"center\"}");
            return;
        }
        _sawText = true;

        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (!IsKnownField(kKnownLabelTextFields,
                              std::size(kKnownLabelTextFields), key)) {
                Error(child, "text 里的字段 \"" + key + "\" 不认识（已知："
                                 + JoinKnownFields(kKnownLabelTextFields,
                                                   std::size(kKnownLabelTextFields))
                                 + "）");
                continue;
            }

            if (key == "content") {
                if (child.IsString()) {
                    _config.data.text = child.text;
                } else {
                    Error(child, "text.content 必须是字符串"
                                 "（字面量，或「占位符」——语言表里的 key）");
                }
            } else if (key == "fontSize") {
                // 数字或语义字号（`"$body"`，B8）：读取器自己负责报错与校验。
                _config.data.style.fontSize = ReadFontSize(child, "text.fontSize");
            } else if (key == "lineSpacing") {
                const double number = ReadNumber(child, "text.lineSpacing");
                if (number <= 0.0) {
                    Error(child, "text.lineSpacing 必须大于 0（1 = 行与行紧贴，"
                                 "常见值是 1.2~1.4；现在是 "
                                     + std::to_string(number) + "）");
                } else {
                    _config.data.style.lineSpacing = static_cast<float>(number);
                }
            } else if (key == "path") {
                ReadFontPaths(child, "text.path",
                              &_config.data.style.fontPath,
                              &_config.data.style.fontPaths);
            } else if (key == "align") {
                int parsed = 0;
                if (ReadEnum(child, "text.align",
                             {{"left", 0}, {"center", 1}, {"right", 2}},
                             parsed)) {
                    _config.data.style.align
                        = static_cast<inking::TextAlignH>(parsed);
                }
            } else if (key == "alignV") {
                int parsed = 0;
                if (ReadEnum(child, "text.alignV",
                             {{"top", 0}, {"middle", 1}, {"bottom", 2}},
                             parsed)) {
                    _config.data.style.alignV
                        = static_cast<inking::TextAlignV>(parsed);
                }
            } else if (key == "padding") {
                ReadSpacing(child, "text.padding", _config.data.style.padding);
            } else if (key == "color") {
                _config.data.color = ReadColorSource(
                    child, "text.color", true, &_config.data.colorToken);
            } else if (key == "wrap") {
                int parsed = 0;
                if (ReadEnum(child, "text.wrap",
                             {{"noWrap", 0}, {"word", 1}, {"anywhere", 2}},
                             parsed)) {
                    _config.data.wrap = static_cast<inking::TextWrap>(parsed);
                }
            } else if (key == "overflow") {
                int parsed = 0;
                if (ReadEnum(child, "text.overflow",
                             {{"clip", 0}, {"ellipsis", 1}}, parsed)) {
                    _config.data.overflow
                        = static_cast<inking::TextOverflow>(parsed);
                }
            } else if (key == "maxLines") {
                const double number = ReadNumber(child, "text.maxLines");
                if (number < 0.0
                    || std::fabs(number - std::floor(number + 0.5)) > 1e-9) {
                    Error(child, "text.maxLines 必须是不小于 0 的整数"
                                 "（0 = 不限；现在是 " + std::to_string(number)
                                     + "）");
                } else {
                    _config.data.maxLines = static_cast<int>(number + 0.5);
                }
            }
        }
    }

    /// 收尾：name / 宽度的共用规矩 + 标签专属的两条。
    void Finish() {
        // 高度可省（0 = 由内容撑高），所以 requireHeight = false。
        CheckNameAndSize(_config.name, _sawNameField, _sawWidth, _sawHeight,
                         /*requireHeight=*/false);

        // 标签的 name **必须**是合法标识符：它没有按名字查的库（对比按钮有
        // `ButtonLibrary`），生成不出类就等于没有任何用法。
        if (!_failed && !IsCxxIdentifier(_config.name)) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "标签的 name \"" + _config.name
                    + "\" 不是合法的 C++ 标识符（只能字母数字下划线、不以数字"
                      "开头）。按钮允许这种名字——它还有「按字符串名字查」那条路；"
                      "标签没有库可查，名字不合法就没有任何用法"});
            _failed = true;
        }

        if (!_sawText) {
            // 空标签不是错误（运行期可以 `SetText` 填），但十有八九是漏写了。
            Warn("没有 text 段：这是一个**空标签**，运行时得自己 SetText() 才有字");
        }

        _config.identifier = MakeNameConstant(_config.name);
        _config.data.name = _config.name;
        _config.warnings = std::move(_warnings);

        // 认领名字：与按钮 / 面板共用同一份 claimedNames（名字全局唯一）。
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            _result.labels.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 静态图片（Image，A11）
// ---------------------------------------------------------------------------

/**
 * 图片配置认的字段（CXXCSS.md §3.10）：**按钮那套减去三态 / 文字 / 过渡**，
 * 加上 `runtime` 那个"图从运行期来"的开关。
 *
 * 没有 `layout`（图片不是容器），也没有 `dynamic`（图片只有静态档：换图只标
 * 重绘脏，几何不动）。
 */
/**
 * 图片配置里**明确不接受**的字段：写了就报错，而不是静默忽略。
 *
 * 这一条是有意的严格：图片就是"没有 onClicked 的 button"，所以它没有三态、
 * 没有文字、没有过渡。若把 `hover` 静默吃掉，用户会以为"悬停能换图"、
 * 调试半天才发现是没生效——那正是"未知字段一律报错"想避免的事。
 */
const char* const kRejectedImageFields[] = {
    "hover", "onclicked", "text", "dynamic", "layout", "transition",
};

/// 路径看起来是不是绝对路径（Windows 盘符 / UNC / POSIX 三种写法）。
bool LooksAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return path.size() >= 2 && path[1] == ':';
}

/**
 * @brief 静态图片的加载器（A11）。
 *
 * 数据落到 `inking::ImageData`——它是 `inking::ButtonData` 的派生，所以这边**逐字
 * 复用**按钮那套读取与校验，只做两件按钮没有的事：
 *
 *   1. **只收 `normal` 一态**（`hover` / `onclicked` / `text` / `transition` 报错）；
 *   2. 认一个 `runtime` 开关，它决定 `normal.source` 是哪种路径：
 *      - `runtime` 省略 / false：**绝对路径**，生成期把文件字节烘成静态数组
 *        （照旧行为）。读不到文件在生成期就报错；
 *      - `"runtime": true`：**相对资源根**的路径（默认 exe 旁的 `assets/`），
 *        **生成期一个字都不读**——产物里只有那个路径字符串，字节由运行期的
 *        `inking::AssetLoad` 去拿（可带轻量混淆）。这就是"别把所有图烘进 exe"。
 */
class ImageLoader : public ConfigReader {
public:
    ImageLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        // `$extends` 的来源（T-10）：合并已经在 `LoadConfigInto` 里做完，
        // 这里只抄一份来源串给生成物注释用；字段本身由 `IsDirectiveField` 跳过。
        if (const JsonValue* extends = root.Find("$extends")) {
            if (extends->IsString()) {
                _config.extendsFrom = extends->text;
            }
        }

        // 第一趟：先收 `runtime` 开关。
        //
        // 它决定后面处理 `normal` 时要不要在生成期读文件，而 json 里的字段顺序
        // 是用户随手写的（`runtime` 写在 `normal` 后面完全正当）——所以这里
        // 必须两趟，不能让"开关写的位置"影响语义。
        for (const auto& field : root.fields) {
            if (field.first != "runtime") {
                continue;
            }
            if (field.second.IsBool()) {
                _config.runtimeAsset = field.second.boolean;
                _sawRuntime = true;
            } else {
                Error(field.second,
                      "runtime 必须是 true 或 false（true = 图在运行期从资源根读，"
                      "false / 省略 = 生成期把文件字节烘进来）");
            }
        }

        // 第二趟：其余字段。
        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key) || key == "runtime") {
                continue;  // 注释跳过；runtime 第一趟已经收过
            }
            if (IsKnownField(kRejectedImageFields,
                             std::size(kRejectedImageFields), key)) {
                Error(value, "图片配置不接受 \"" + key
                                 + "\"：图片就是「没有 onClicked 的 button」"
                                   "——它没有三态、没有文字、也没有过渡"
                                   "（要那些请用 CXXCSS/Button/<名字>.json）");
                continue;
            }
            if (!IsKnownField(kKnownImageFields, std::size(kKnownImageFields),
                              key)) {
                Error(value, "字段 \"" + key + "\" 不认识（图片认的字段："
                                 + JoinKnownFields(kKnownImageFields,
                                                   std::size(kKnownImageFields))
                                 + "）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "width") {
                _config.button.data.width = ReadSize(value, "width");
                _sawWidth = true;
            } else if (key == "height") {
                _config.button.data.height = ReadSize(value, "height");
                _sawHeight = true;
            } else if (key == "normal") {
                ReadImageAppearance(value);
            } else if (key == "selfAnchorX") {
                ReadAnchor(value, "selfAnchorX",
                           _config.button.data.selfAnchor.x);
            } else if (key == "selfAnchorY") {
                ReadAnchor(value, "selfAnchorY",
                           _config.button.data.selfAnchor.y);
            } else if (key == "traceAnchorX") {
                ReadAnchor(value, "traceAnchorX",
                           _config.button.data.traceAnchor.x);
            } else if (key == "traceAnchorY") {
                ReadAnchor(value, "traceAnchorY",
                           _config.button.data.traceAnchor.y);
            } else if (key == "offsetX") {
                _config.button.data.offsetX = ReadNumber(value, "offsetX");
            } else if (key == "offsetY") {
                _config.button.data.offsetY = ReadNumber(value, "offsetY");
            } else if (key == "layoutItem") {
                ReadLayoutItem(value, _config.button.data.layoutItem);
            } else if (key == "zIndex") {
                _config.button.data.zIndex
                    = static_cast<int>(ReadNumber(value, "zIndex"));
            } else if (key == "visible") {
                if (value.IsBool()) {
                    _config.button.data.visible = value.boolean;
                } else {
                    Error(value, "visible 必须是 true 或 false");
                }
            } else if (key == "shape") {
                _sawShape = true;
                ReadShape(value, _config.button.data.shape, nullptr);
            } else if (key == "stroke") {
                ReadStroke(value, _config.button.data.stroke);
            } else if (key == "shadow") {
                ReadShadow(value, _config.button.data.shadow);
            }
        }

        Finish();
    }

private:
    ImageConfig _config;
    bool _sawNameField = false;
    bool _sawWidth = false;
    bool _sawHeight = false;
    bool _sawShape = false;
    bool _sawRuntime = false;

    /**
     * 读 `normal` 段：图片**只认 `type` / `source` / `fit`** 三样。
     *
     * 为什么不复用按钮那份 `ReadAppearance`（它认 type / source / transition /
     * transform / width / height / fit，还带 svg 子集）：图片只有一态、没有过渡、
     * 也没有逐态尺寸，复用会让"图片配了 transition"这种写法**静默生效一半**。
     * 这里只认三样，别的一律报错——写错地方比读不出来好查（同 §6 第 34 条那种
     * "配置写了却没生效"的坑）。
     *
     * `svg` 外观也**明确不支持**：那要生成期把 SVG 转成"形状 + 填充色"（按钮那条
     * 路，`_svg` 那套状态），图片这一版认 `color` 与 `img` 两种。
     */
    void ReadImageAppearance(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value,
                  "normal 必须是对象，形如 {\"type\":\"img\","
                  "\"source\":\"images/card.png\"}");
            return;
        }
        _config.sawNormal = true;

        for (const auto& entry : value.fields) {
            if (!IsKnownField(kKnownImageAppearanceFields,
                              std::size(kKnownImageAppearanceFields),
                              entry.first)) {
                Error(entry.second,
                      "normal 里的字段 \"" + entry.first
                          + "\" 不认识（图片这一层只认 "
                          + JoinKnownFields(kKnownImageAppearanceFields,
                                            std::size(kKnownImageAppearanceFields))
                          + "；三态 / 过渡 / 逐态尺寸 / svg 都是按钮的东西）");
            }
        }

        const JsonValue* type = value.Find("type");
        const JsonValue* source = value.Find("source");
        if (type == nullptr || !type->IsString()) {
            Error(value, "normal 缺少 type（图片认 color 与 img 两种）");
            return;
        }

        std::string typeName = type->text;
        std::transform(typeName.begin(), typeName.end(), typeName.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });

        inking::ButtonAppearance& out = _config.button.data.normal;
        if (typeName == "color") {
            out.type = inking::AppearanceType::Color;
            if (source == nullptr) {
                Error(value, "normal 缺少 source（color 要写 \"r|g|b|a\"）");
                return;
            }
            out.color = ReadColorSource(*source, "normal");
            out.source = source->IsString() ? source->text : std::string();
        } else if (typeName == "img") {
            out.type = inking::AppearanceType::Image;
            if (source == nullptr || !source->IsString()) {
                Error(value, "normal 的 img 需要 source（图片路径）");
                return;
            }
            out.source = source->text;
            if (!_config.runtimeAsset) {
                // 生成期烘那条路：**现在**就把文件读进来——"文件没了"在构建期
                // 就报出来，而不是等运行期画出一片空白。
                const std::string problem = ReadImageBytes(
                    source->text, _config.button.imageBytes[0]);
                if (!problem.empty()) {
                    Error(*source, problem);
                    _failed = true;
                    return;
                }
                out.imageBytes = _config.button.imageBytes[0].data();
                out.imageSize = _config.button.imageBytes[0].size();
            }
            // runtime = true：**一个字都不读**，source 只是个路径字符串
            // （运行期由 inking::AssetLoad 去拿，见 asset/InkingAsset.h）。
        } else if (typeName == "svg") {
            Error(*type,
                  "图片这一版不支持 svg 外观（它要生成期把 SVG 转成形状，"
                  "那是按钮那条路）：请用 type = color 或 img");
            return;
        } else {
            Error(*type, "normal.type \"" + type->text
                             + "\" 不认识（只支持 color、img）");
            return;
        }

        if (const JsonValue* fitField = value.Find("fit")) {
            if (!fitField->IsString()) {
                Error(*fitField,
                      "normal.fit 必须是字符串（fill / contain / cover）");
            } else {
                std::string fitName = fitField->text;
                std::transform(fitName.begin(), fitName.end(), fitName.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if (fitName == "fill") {
                    out.imageFit = inking::ImageFit::Fill;
                } else if (fitName == "contain") {
                    out.imageFit = inking::ImageFit::Contain;
                } else if (fitName == "cover") {
                    out.imageFit = inking::ImageFit::Cover;
                } else {
                    Error(*fitField, "normal.fit \"" + fitField->text
                                         + "\" 不认识（fill / contain / cover）");
                }
            }
        }
    }

    void Finish() {
        CheckNameAndSize(_config.name, _sawNameField, _sawWidth, _sawHeight);

        if (!_config.sawNormal) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "图片必须有 normal 段：图片只有这一态（它不悬停、不按下、不点击）"});
            _failed = true;
        }

        // 同标签：名字必须是合法标识符（图片也没有"按名字查"的库）。
        if (!_failed && !IsCxxIdentifier(_config.name)) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "图片的 name \"" + _config.name
                    + "\" 不是合法的 C++ 标识符（只能字母数字下划线、不以数字"
                      "开头）：图片没有按名字查的库，名字不合法就没有任何用法"});
            _failed = true;
        }

        // 两条路的路径写法不能混：`runtime` 要相对路径（相对资源根），
        // 生成期烘要绝对路径（生成器的工作目录不一定是哪儿）。
        if (!_failed && _config.runtimeAsset
            && LooksAbsolutePath(_config.button.data.normal.source)) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "runtime = true 时 source 要写**相对资源根**的路径（例如 "
                "\"images/card.png\"），而这里是绝对路径 \""
                    + _config.button.data.normal.source
                    + "\"——绝对路径是「生成期把字节烘进来」那条路的写法"
                      "（去掉 runtime 就是那条）"});
            _failed = true;
        }
        _config.identifier = MakeNameConstant(_config.name);
        _config.button.data.name = _config.name;

        // 生成期烘的字节：把视图指到 vector 的缓冲上。
        // （`std::move` 进 result 是转移所有权，缓冲地址不变，所以先设指针也安全。）
        if (!_config.button.imageBytes[0].empty()) {
            _config.button.data.normal.type = inking::AppearanceType::Image;
            _config.button.data.normal.imageBytes
                = _config.button.imageBytes[0].data();
            _config.button.data.normal.imageSize
                = _config.button.imageBytes[0].size();
        }

        _config.warnings = std::move(_warnings);
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            _result.images.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 语言表（Text，A7 的生成器段）
// ---------------------------------------------------------------------------

/**
 * @brief 语言表的加载器。
 *
 * 它是四种类型里唯一**不是组件**的那个：一张全局表（语言 + 占位符 → 各语言的
 * 串），运行期由 `inking::InstallTextTable` 装进 `include/text/InkingTextTable.h`
 * 的那份状态里。所以它没有尺寸 / 锚点 / 形状，也不进命中表与绘制列表。
 *
 * 三条规矩：
 *   1. **只允许一份**：安装的语义就是"替换整张表"，两张只有一张生效——与其
 *      让用户猜哪张赢，不如在这里报错（报错里指出已经在的那份文件）；
 *   2. `entries` 里每条的长度必须**等于** `languages` 的数量（位置就是对应关系：
 *      第 i 个串是第 i 门语言的译文）。缺一门要报错，而不是悄悄留空；
 *   3. `default` 必须是 `languages` 里的一门。
 */
class TextLoader : public ConfigReader {
public:
    TextLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key)) {
                continue;
            }
            if (!IsKnownField(kKnownTextTableFields,
                              std::size(kKnownTextTableFields), key)) {
                Error(value, "字段 \"" + key + "\" 不认识（语言表认的字段："
                                 + JoinKnownFields(
                                     kKnownTextTableFields,
                                     std::size(kKnownTextTableFields))
                                 + "；它是全局表，没有 width / height / 锚点）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "languages") {
                ReadLanguages(value);
            } else if (key == "default") {
                if (value.IsString() && !value.text.empty()) {
                    _config.defaultLanguage = value.text;
                } else {
                    Error(value, "default 必须是字符串（语言代号，例如 \"zh\"）");
                }
            } else if (key == "entries") {
                ReadEntries(value);
            }
        }

        Finish();
    }

private:
    /// 一条还没对账长度的条目：`languages` 可能写在 `entries` **后面**
    /// （json 里字段顺序不该有意义——读 `transition` 时同一条规矩）。
    struct RawEntry {
        std::string key;
        std::vector<std::string> translations;
        const JsonValue* where = nullptr;  ///< 报错要指到那一条
    };

    TextConfig _config;
    bool _sawNameField = false;
    bool _sawLanguages = false;
    bool _sawEntries = false;
    std::vector<RawEntry> _rawEntries;

    void ReadLanguages(const JsonValue& value) {
        if (!value.IsArray() || value.items.empty()) {
            Error(value,
                  "languages 必须是**非空**字符串数组，例如 [\"zh\",\"en\"]"
                  "（顺序就是 entries 里各语言串的顺序）");
            return;
        }
        _sawLanguages = true;
        for (const JsonValue& item : value.items) {
            if (!item.IsString() || item.text.empty()) {
                Error(item, "languages 里每一项都必须是非空字符串"
                            "（语言代号，例如 zh / en）");
                continue;
            }
            bool duplicate = false;
            for (const std::string& known : _config.table.languages) {
                if (known == item.text) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                Error(item, "语言代号 \"" + item.text + "\" 重复了");
                continue;
            }
            _config.table.languages.push_back(item.text);
        }
    }

    void ReadEntries(const JsonValue& value) {
        if (!value.IsObject() || value.fields.empty()) {
            Error(value,
                  "entries 必须是**非空**对象：一条一个占位符，值是该 key 在各"
                  "语言下的串（顺序与 languages 对应），"
                  "形如 {\"menu.start\":[\"开始\",\"Start\"]}");
            return;
        }
        _sawEntries = true;
        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (key.empty()) {
                Error(child, "entries 里的占位符不能是空串");
                continue;
            }
            if (!child.IsArray()) {
                Error(child, "entries[\"" + key + "\"] 必须是数组"
                             "（每门语言一个串，顺序与 languages 对应）");
                continue;
            }
            RawEntry raw;
            raw.key = key;
            raw.where = &child;
            for (const JsonValue& item : child.items) {
                if (!item.IsString()) {
                    Error(item, "entries[\"" + key + "\"] 里只能放字符串"
                                "（空串表示这门语言没译文，运行期会回退成 key 本身）");
                    continue;
                }
                raw.translations.push_back(item.text);
            }
            _rawEntries.push_back(std::move(raw));
        }
    }

    /// 表里有哪些语言（报错文案里列出来）。
    std::string JoinedLanguages() const {
        std::string joined;
        for (const std::string& language : _config.table.languages) {
            if (!joined.empty()) {
                joined += " / ";
            }
            joined += language;
        }
        return joined.empty() ? std::string("（一门都没有）") : joined;
    }

    /// 收尾：name 的三条规矩 + 语言表专属的四条（只允许一份等）。
    void Finish() {
        // 语言表也占一个名字（与按钮 / 面板 / 标签共用同一份 claimedNames），
        // 但它没有尺寸，所以不走 CheckNameAndSize。
        CheckName(_config.name, _sawNameField);

        if (!_sawLanguages) {
            _result.errors.push_back(CxxcssError{
                _path, 0, "缺少 languages（这张表有哪几门语言）"});
            _failed = true;
        }
        if (!_sawEntries) {
            _result.errors.push_back(CxxcssError{
                _path, 0, "缺少 entries（占位符 → 各语言的串）"});
            _failed = true;
        }

        // 长度对账（要等 languages 与 entries 都读完）。
        if (_sawLanguages && _sawEntries && !_failed) {
            const std::size_t count = _config.table.languages.size();
            for (const RawEntry& raw : _rawEntries) {
                if (raw.translations.size() == count) {
                    continue;
                }
                Error(*raw.where,
                      "entries[\"" + raw.key + "\"] 有 "
                          + std::to_string(raw.translations.size())
                          + " 个串，而 languages 有 " + std::to_string(count)
                          + " 门（位置对应：一门一个串）");
            }
        }

        // 缺省语言必须是表里的一门（写错了就该报，不能悄悄退回第一门）。
        //
        // 这里自己走一遍 language 列表，**不调 `TextTable::LanguageIndex`**：
        // 那个成员函数的实现在 `src/text/InkingTextTable.cpp`（属于 ink_core），
        // 而生成器**只依赖标准库、不链接 ink_core**（见 cmake/inkgen.cmake）——
        // 调它就是把"SDF 与 SDL 的世界"拖进构建期工具。
        bool defaultKnown = false;
        for (const std::string& language : _config.table.languages) {
            if (language == _config.defaultLanguage) {
                defaultKnown = true;
                break;
            }
        }
        if (_sawLanguages && !_failed && !_config.defaultLanguage.empty()
            && !defaultKnown) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "default \"" + _config.defaultLanguage
                    + "\" 不在 languages 里（这门表里有：" + JoinedLanguages()
                    + "）"});
            _failed = true;
        }

        // 只允许一份：`InstallTextTable` 的语义是"替换整张表"。
        if (!_failed && !_result.textTables.empty()) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "语言表只能有一份（这一批里已经有 "
                    + _result.textTables.front().filePath
                    + " 了）。多份请合成一份 json——entries 本来就是"
                      "「占位符 → 各语言的串」的那张表"});
            _failed = true;
        }

        _config.identifier = MakeNameConstant(_config.name);
        _config.warnings = std::move(_warnings);

        // 认领名字：**不过滤 _failed**（同 ButtonLoader / PanelLoader）。
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            for (RawEntry& raw : _rawEntries) {
                inking::TextEntry entry;
                entry.key = std::move(raw.key);
                entry.translations = std::move(raw.translations);
                _config.table.entries.push_back(std::move(entry));
            }
            _result.textTables.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 主题表（Theme，B8）
// ---------------------------------------------------------------------------

/**
 * @brief 主题表的加载器。
 *
 * 它是**生成器的输入**，不是组件：`colors` / `fontSizes` 两张表，组件配置里的
 * `"$surface"` / `"$body"` 在**生成期**被换成这里的值（见 `ReadColorSource` /
 * `ReadFontSize`）。
 *
 * **可以有很多份**（2026-10-18 起）：颜色 token 的名字会跟着组件数据发出去，
 * 运行期换肤按名字重查（`theme/InkingTheme.h`）。其中一份是**烘焙主题**——
 * `$token` 按它解析成字面量，也就是不切主题时看到的颜色；标 `"default": true`
 * 的就是它，没标则按文件名排序取第一份。
 *
 * 四条规矩：
 *   1. **每份都要覆盖组件用到的全部颜色 token**（跨表校验在 `LoadConfigs` 收尾）；
 *   2. 主题里的值**必须是字面量**——写 `"$other"` 是间接引用，直接报错
 *      （"token 指向 token"迟早绕成环）；
 *   3. 没被任何组件用到的 token 不算错；组件用了**主题里没有**的 token 才算错，
 *      而那个报错发生在组件那一侧（顺带把主题里有的列出来）。
 */
class ThemeLoader : public ConfigReader {
public:
    ThemeLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key)) {
                continue;
            }
            if (!IsKnownField(kKnownThemeFields, std::size(kKnownThemeFields),
                              key)) {
                Error(value,
                      "字段 \"" + key + "\" 不认识（主题表认的字段："
                          + JoinKnownFields(kKnownThemeFields,
                                            std::size(kKnownThemeFields))
                          + "；它是生成器的输入，没有尺寸 / 锚点 / 形状）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "colors") {
                ReadColors(value);
            } else if (key == "fontSizes") {
                ReadFontSizes(value);
            } else if (key == "default") {
                // 多份主题时用它指定"烘焙主题"（`$token` 按它解析成字面量）。
                if (value.IsBool()) {
                    _config.isDefault = value.boolean;
                } else {
                    Error(value, "default 必须是 true / false");
                }
            }
        }

        Finish();
    }

private:
    ThemeConfig _config;
    bool _sawNameField = false;
    bool _sawColors = false;
    bool _sawFontSizes = false;

    /// token 名本身的写法检查（两张表共用）：非空、不带 `$`。
    bool CheckToken(const std::string& table, const std::string& token,
                    const JsonValue& child) {
        if (token.empty()) {
            Error(child, table + " 里的 token 不能是空串");
            return false;
        }
        if (token[0] == '$') {
            Error(child, table + " 里的 token 写名字就行（\"" + token.substr(1)
                             + "\"），不用带 $；带 $ 的是**用它**的地方");
            return false;
        }
        return true;
    }

    /// `"colors": { "surface": "0.1|0.11|0.15|1" }`
    void ReadColors(const JsonValue& value) {
        if (!value.IsObject() || value.fields.empty()) {
            Error(value, "colors 必须是**非空**对象：一条一个语义色，"
                         "形如 {\"surface\":\"0.1|0.11|0.15|1\"}");
            return;
        }
        _sawColors = true;
        for (const auto& entry : value.fields) {
            const std::string& token = entry.first;
            const JsonValue& child = entry.second;
            if (!CheckToken("colors", token, child)) {
                continue;
            }
            if (child.IsString() && !child.text.empty() && child.text[0] == '$') {
                Error(child, "主题里的值必须是**字面量颜色**（\"r|g|b|a\"），"
                             "不能是 \"$" + child.text.substr(1)
                                 + "\"（token 指向 token 迟早绕成环）");
                continue;
            }
            // 值本身复用同一个颜色解析器（`$` 那一支上面刚拦掉）。
            const std::uint32_t color
                = ReadColorSource(child, "colors." + token);
            _config.colors.emplace_back(token, color);
        }
    }

    /// `"fontSizes": { "body": 15, "title": 24 }`
    void ReadFontSizes(const JsonValue& value) {
        if (!value.IsObject() || value.fields.empty()) {
            Error(value, "fontSizes 必须是**非空**对象：一条一个语义字号，"
                         "形如 {\"body\":15,\"title\":24}");
            return;
        }
        _sawFontSizes = true;
        for (const auto& entry : value.fields) {
            const std::string& token = entry.first;
            const JsonValue& child = entry.second;
            if (!CheckToken("fontSizes", token, child)) {
                continue;
            }
            if (child.IsString()) {
                Error(child, "主题里的字号必须是数字，不能是 \"" + child.text
                                 + "\"（token 指向 token 迟早绕成环）");
                continue;
            }
            const double number = ReadNumber(child, "fontSizes." + token);
            if (number <= 0.0) {
                Error(child, "fontSizes." + token + " 必须大于 0（现在是 "
                                 + std::to_string(number) + "）");
                continue;
            }
            _config.fontSizes.emplace_back(token, number);
        }
    }

    /// 收尾：名字三条规矩 + "不能是空表" + 只允许一份。
    void Finish() {
        CheckName(_config.name, _sawNameField);

        if (!_sawColors && !_sawFontSizes) {
            _result.errors.push_back(CxxcssError{
                _path, 0, "主题表是空的：colors 与 fontSizes 至少要写一个"});
            _failed = true;
        }

        // 允许多份主题：**第一份是烘焙主题**（`$token` 按它解析成字面量），
        // 其余几份供运行期换肤用。跨表校验（每张都得有组件用到的 token）
        // 在 `LoadConfigs` 收尾做——那时才知道"到底用了哪些 token"。

        _config.warnings = std::move(_warnings);
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            _result.themes.push_back(std::move(_config));
        }
    }
};

// ---------------------------------------------------------------------------
// 全局配置（Config）：设计画布尺寸 + 窗口启动设置
// ---------------------------------------------------------------------------

/**
 * @brief 全局配置的加载器（`CXXCSS/config.json`）。
 *
 * 两个段：
 *   - `design`（**必填**）：设计画布宽高 → 生成一对常量 + `SetDesignSize`；
 *   - `window`（可选）：窗口启动设置 → 生成常量 + `ApplyWindowConfig`。
 *
 * 产物是一个 `ApplyConfig()`（见 `Emit.cpp`），**不是**类、**不是**运行期查表。
 *
 * 五条规矩：
 *   1. `design` **必填**——一份什么都不设的配置没有存在意义（那和"没有配置"
 *      无法区分，而两者想要的效果可能正好相反）；
 *   2. `width` / `height` 都要有，且是 **1..kMaxDesignSize 的整数**：
 *      设计坐标是整数格，写 1280.5 只会让"一眼看出对齐没对齐"这件事失效；
 *   3. `window` 里的每一项**都是可选的**，没写 = 不动库里的默认值（生成器
 *      不抄一份默认值，抄了就是第二份真相）；
 *   4. **只允许一份**（同语言表 / 主题表）：两份配置只有一份生效，
 *      与其让人猜哪份赢，不如生成期报错；
 *   5. `name` 不参与任何检查——它不生成符号、不是查找键，写不写都行
 *      （收它只是为了让"照抄其他配置的模板"不会平白报一条错）。
 */
class ConfigLoader : public ConfigReader {
public:
    ConfigLoader(std::string path, LoadResult& result)
        : ConfigReader(std::move(path), result) {
        _config.filePath = _path;
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (IsDirectiveField(key) || key == "name") {
                continue;
            }
            if (!IsKnownField(kKnownConfigFields, std::size(kKnownConfigFields),
                              key)) {
                Error(value,
                      "字段 \"" + key + "\" 不认识（全局配置认的字段："
                          + JoinKnownFields(kKnownConfigFields,
                                            std::size(kKnownConfigFields))
                          + "；它只管设计画布与窗口设置，没有锚点 / 组件外观）");
                continue;
            }
            if (key == "design") {
                ReadDesign(value);
            } else if (key == "window") {
                ReadWindow(value);
            }
        }

        Finish();
    }

private:
    GlobalConfig _config;
    bool _sawDesign = false;

    /// `"design": { "width": 1280, "height": 720 }`
    void ReadDesign(const JsonValue& value) {
        if (!value.IsObject() || value.fields.empty()) {
            Error(value, "design 必须是**非空**对象："
                         "形如 {\"width\":1280,\"height\":720}");
            return;
        }
        if (_sawDesign) {
            // 重复键由 JSON 解析器拦（Json.h 顶上那条），这里只是防御：
            // 万一以后允许数组写法，也别悄悄保留后一份。
            Error(value, "design 只能写一次");
            return;
        }
        _sawDesign = true;

        bool sawWidth = false;
        bool sawHeight = false;
        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (IsDirectiveField(key)) {
                continue;
            }
            if (!IsKnownField(kKnownDesignFields, std::size(kKnownDesignFields),
                              key)) {
                Error(child,
                      "design 里的字段 \"" + key
                          + "\" 不认识（认的字段："
                          + JoinKnownFields(kKnownDesignFields,
                                            std::size(kKnownDesignFields))
                          + "；letterbox 模式 / DPI 策略等扩展位还没有实装，"
                            "写了也不会生效，所以不收）");
                continue;
            }
            if (key == "width") {
                sawWidth = true;
                _config.width = ReadSide(child, "design.width");
            } else {
                sawHeight = true;
                _config.height = ReadSide(child, "design.height");
            }
        }

        if (!sawWidth) {
            Error(value, "design 缺少 width（设计坐标，必须大于 0）");
        }
        if (!sawHeight) {
            Error(value, "design 缺少 height（设计坐标，必须大于 0）");
        }
    }

    /// 一条边：整数、1..`inking::kMaxDesignSize`。上限与运行期同源，不另立一份。
    int ReadSide(const JsonValue& value, const std::string& field) {
        const double number = ReadNumber(value, field);
        if (number < 1.0 || number > static_cast<double>(inking::kMaxDesignSize)) {
            Error(value, field + " 必须在 1.."
                             + std::to_string(inking::kMaxDesignSize)
                             + " 之间（现在是 " + NumberText(number) + "）");
            return 0;
        }
        const int rounded = static_cast<int>(number + 0.5);
        if (number != static_cast<double>(rounded)) {
            Error(value, field + " 必须是整数（现在是 " + NumberText(number)
                             + "）：设计坐标按整像素对位");
            return 0;
        }
        return rounded;
    }

    /// 报错里的数字：整数就不带小数点（`std::to_string` 会写成 `720.500000`）。
    static std::string NumberText(double number) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%g", number);
        return buffer;
    }

    /// JSON 布尔。**只认 true / false**：字符串 "true" 一律报错——
    /// 这里的每个字段最后都会变成一个 C++ 字面量，含糊的写法会一路含糊到产物里。
    bool ReadBool(const JsonValue& value, const std::string& field) {
        if (!value.IsBool()) {
            Error(value, field + " 必须是 true / false（不要加引号），这里是"
                             + value.KindName());
            return false;
        }
        return value.boolean;
    }

    /**
     * `"window": { "borderless": true, "background": "$surface", "startScene": "main" }`
     *
     * 每一项都是**可选的**：没写就不进产物、运行期也就不碰它（保持库默认值）。
     * 这条和"生成器不抄一份默认值"是一件事——抄了就是第二份真相。
     */
    void ReadWindow(const JsonValue& value) {
        if (!value.IsObject() || value.fields.empty()) {
            Error(value, "window 必须是**非空**对象：形如 {\"borderless\":true}");
            return;
        }

        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (IsDirectiveField(key)) {
                continue;
            }
            if (!IsKnownField(kKnownWindowFields,
                              std::size(kKnownWindowFields), key)) {
                Error(child,
                      "window 里的字段 \"" + key
                          + "\" 不认识（认的字段："
                          + JoinKnownFields(kKnownWindowFields,
                                            std::size(kKnownWindowFields))
                          + "。**没有回读口的设置不收**：写了不生效没法自证，"
                            "那就是新的静默失效——要加新字段，先给窗口补 getter）");
                continue;
            }
            if (key == "borderless") {
                _config.borderless = ReadBool(child, "window.borderless");
            } else if (key == "roundedCorners") {
                _config.roundedCorners
                    = ReadBool(child, "window.roundedCorners");
            } else if (key == "fullscreen") {
                _config.fullscreen = ReadBool(child, "window.fullscreen");
            } else if (key == "transparent") {
                // 顺序无关（这里只解析）；`ApplyConfig()` 生成时会把 transparent
                // 排在 background 之前——背景 alpha 的判定看的就是它。
                _config.transparent = ReadBool(child, "window.transparent");
            } else if (key == "background") {
                // 与组件外观同一个解析器：`"r|g|b|a"`（0..1 浮点）或 `"$token"`。
                _config.background = ReadColorSource(child, "window.background",
                                                    /*isSourceField=*/false);
            } else if (key == "startScene") {
                if (!child.IsString() || child.text.empty()) {
                    Error(child, "window.startScene 必须是非空字符串（场景名）");
                } else {
                    _config.startScene = child.text;
                }
            }
        }
    }

    /// 收尾：`design` 必填 + 只允许一份。
    void Finish() {
        if (!_sawDesign && !_failed) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "全局配置是空的：design 段没写。设计画布没有默认以外的东西可设——"
                "要改尺寸就写 {\"design\":{\"width\":...,\"height\":...}}，"
                "不想改就别建这个文件"});
            _failed = true;
        }

        if (!_failed && !_result.configs.empty()) {
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "全局配置只能有一份（这一批里已经有 "
                    + _result.configs.front().filePath
                    + " 了）。换设计尺寸 / 窗口设置请改那一份"});
            _failed = true;
        }

        _config.warnings = std::move(_warnings);

        if (!_failed) {
            _result.configs.push_back(std::move(_config));
        }
    }
};

}  // namespace

std::string CxxcssError::Format() const {
    // file 为空表示"message 里已经带了定位"（例如 JSON 语法错，ParseJson 给的
    // 错误串本身就带文件名和行列）。这种情况下不要再补一个空前缀加冒号。
    if (file.empty()) {
        return message;
    }

    std::string text = file;
    if (line > 0) {
        text += ":" + std::to_string(line);
    }
    text += ": " + message;
    return text;
}

std::string FileStem(const std::string& path) {
    std::size_t begin = path.find_last_of("/\\");
    begin = (begin == std::string::npos) ? 0 : begin + 1;

    std::string name = path.substr(begin);
    const std::string suffix = ".json";
    if (name.size() > suffix.size()
        && name.compare(name.size() - suffix.size(), suffix.size(), suffix)
               == 0) {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

bool IsCxxIdentifier(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name[0] >= '0' && name[0] <= '9') {
        return false;
    }
    for (char ch : name) {
        const bool letter = (ch >= 'a' && ch <= 'z')
                         || (ch >= 'A' && ch <= 'Z') || ch == '_';
        const bool digit = ch >= '0' && ch <= '9';
        if (!letter && !digit) {
            return false;
        }
    }
    return true;
}

std::string MakeNameConstant(const std::string& name) {
    if (!IsCxxIdentifier(name)) {
        // 名字里有点或连字符（例如示例里的 button.example）：生成不出类，
        // 这个按钮就只能按字符串名字使用（数据照样会进 RegisterAllButtons）。
        return std::string();
    }

    // `normalButton` → `NormalButton`：首字母大写，变成一个**类名**。
    //
    // 为什么用类名而不是 `kNormalButtonName` 常量：生成的是完整的一个类，
    // 数据烘在类里。用类名当查找键，"拼错名字"就等于"类型不存在"，
    // 比"拼错一个字符串常量"更早、更硬地拦住。
    std::string type = name;
    type[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(type[0])));
    return type;
}

const char* ComponentKindName(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Panel:
            return "面板";
        case ComponentKind::Label:
            return "标签";
        case ComponentKind::Image:
            return "图片";
        case ComponentKind::Text:
            return "语言表";
        case ComponentKind::Theme:
            return "主题表";
        case ComponentKind::Config:
            return "全局配置";
        case ComponentKind::Button:
        default:
            return "按钮";
    }
}

const std::uint32_t* ThemeConfig::FindColor(const std::string& token) const
    noexcept {
    for (const auto& entry : colors) {
        if (entry.first == token) {
            return &entry.second;
        }
    }
    return nullptr;
}

const double* ThemeConfig::FindFontSize(const std::string& token) const noexcept {
    for (const auto& entry : fontSizes) {
        if (entry.first == token) {
            return &entry.second;
        }
    }
    return nullptr;
}

namespace {

/// 路径的父目录名（最后一次出现的分隔符之后那一段）；没有目录时返回空串。
std::string LeafDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    std::string dir = path.substr(0, slash);
    const std::size_t parentSlash = dir.find_last_of("/\\");
    return (parentSlash == std::string::npos) ? dir
                                              : dir.substr(parentSlash + 1);
}

}  // namespace

bool TryComponentKindFromPath(const std::string& path, ComponentKind& out) {
    const std::string leaf = LeafDirectory(path);
    if (leaf.empty()) {
        return false;
    }

    // 归一化：转小写 + 去掉末尾的复数 s。于是 `Button` / `button` / `buttons`
    // 都认；`Panel` / `panels` 同理。大小写不敏感是有意的——`CXXCSS.md` §1
    // 早先写的是"大小写敏感"，但仓库里已经有 `examples/*/buttons/`（小写复数）
    // 这类目录，把规则收得比现实还紧只会逼人改名。
    std::string key;
    key.reserve(leaf.size());
    for (const char ch : leaf) {
        key.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    // `CXXCSS` 根下的文件：**在削复数 s 之前**判，因为 `cxxcss` 结尾本身就是
    // 两个 s，削完变成 `cxxc`（踩过一次：根下的 config.json 认不出来）。
    if (key == "cxxcss") {
        out = ComponentKind::Config;
        return true;
    }

    while (!key.empty() && key.back() == 's') {
        key.pop_back();
    }

    if (key == "button") {
        out = ComponentKind::Button;
        return true;
    }
    if (key == "panel") {
        out = ComponentKind::Panel;
        return true;
    }
    // `Label` / `labels` → 标签；`Text` / `texts` → **语言表**（A7）。
    // 注意 `Text` 这一项容易误会：它是"占位符 → 各语言的串"那张全局表，
    // 不是"文字组件"（文字组件叫 Label）。
    if (key == "label") {
        out = ComponentKind::Label;
        return true;
    }
    if (key == "text") {
        out = ComponentKind::Text;
        return true;
    }
    // `Image` / `images` → **静态图片**（A11）：不带点击回调的按钮。
    if (key == "image") {
        out = ComponentKind::Image;
        return true;
    }
    // `Theme` / `themes` → **主题表**（B8）：语义色 / 语义字号 → 具体值。
    // 它是**生成器的输入**，不是组件（产物里没有主题类）。
    if (key == "theme") {
        out = ComponentKind::Theme;
        return true;
    }
    // `Config` / `configs` → **全局配置**：设计画布尺寸。也不是组件。
    if (key == "config") {
        out = ComponentKind::Config;
        return true;
    }
    return false;
}

LoadResult LoadConfig(const std::string& path) {
    LoadResult result;
    LoadConfigInto(path, result);
    return result;
}

// ---------------------------------------------------------------------------
// `$extends`：单层浅合并（0.2.0 T-10）
// ---------------------------------------------------------------------------
//
// 语义（CXXCSS.md §4.6）：
//   - `"$extends": "Panel/basePanel"`——**相对 CXXCSS 根**、**不带 .json**；
//   - **一层**：父文件的字段被子文件同名字段覆盖；**不支持 A→B→C**；
//   - **对象逐键覆盖，不做深合并**：深合并会让"我想把父的 `text` 整段换掉"
//     变成做不到（子对象里没写的键会从父继承下来）。
//
// 顺序是"**先合并、再校验**"：反过来的话，父文件里写错的字段会被子文件覆盖掉，
// 永远查不出来。

namespace {

/// 路径的父目录。配置路径来自扫描 / 命令行，没有尾分隔符。
std::string ParentDirectoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

/// ASCII 大小写不敏感比较（目录名 / 扩展名这种场景够用）。
bool EqualsIgnoreCase(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        const auto lower = [](unsigned char c) {
            return static_cast<unsigned char>(
                c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        };
        if (lower(a) != lower(b)) {
            return false;
        }
    }
    return true;
}

/// 把路径里的 `\` 统一成 `/`，供"是不是同一个文件"这种粗比较用。
std::string NormalizeSeparators(const std::string& path) {
    std::string out = path;
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    return out;
}

/// 这个文件所属的 **CXXCSS 根**——`$extends` 的路径相对它解析。
std::string CxxcssRootOf(const std::string& path) {
    const std::string dir = ParentDirectoryOf(path);
    if (dir.empty()) {
        return {};
    }
    // 文件直接躺在根下（`CXXCSS/config.json`）：根就是它的父目录。
    // 用**目录名**判断，不用"父目录是不是组件目录"：`CXXCSS` 这个名字本身
    // 也会被 `TryComponentKindFromPath` 认成 Config（`CXXCSS.md` §1 的那条特例）。
    if (EqualsIgnoreCase(LeafDirectory(path), "cxxcss")) {
        return dir;
    }
    return ParentDirectoryOf(dir);
}

/// 递归标上来源文件：合并进来的值报错时要指到**父文件**（T-10 的第 4 条）。
void MarkSourceFile(JsonValue& value, const std::string& file) {
    value.file = file;
    for (JsonValue& item : value.items) {
        MarkSourceFile(item, file);
    }
    for (auto& field : value.fields) {
        MarkSourceFile(field.second, file);
    }
}

/// 子对象覆盖父对象（**只合并顶层**）。
JsonValue MergeExtendsObjects(const JsonValue& child, const JsonValue& parent,
                              const std::string& parentPath) {
    JsonValue merged;
    merged.kind = JsonValue::Kind::Object;
    merged.where = child.where;

    for (const auto& field : parent.fields) {
        if (IsDirectiveField(field.first)) {
            continue;  // 指令字段不继承（父的 $comment / $extends 不往下带）
        }
        JsonValue value = field.second;
        MarkSourceFile(value, parentPath);
        merged.fields.emplace_back(field.first, std::move(value));
    }
    for (const auto& field : child.fields) {
        const auto existing = std::find_if(
            merged.fields.begin(), merged.fields.end(),
            [&field](const auto& entry) { return entry.first == field.first; });
        if (existing != merged.fields.end()) {
            existing->second = field.second;  // 子覆盖父
        } else {
            merged.fields.push_back(field);
        }
    }
    return merged;
}

}  // namespace

/**
 * 配置里写了 `$extends` 就先把父文件读进来做一层浅合并。
 *
 * @return false = 已经报了错（调用方直接返回，不要拿半份配置去生成）。
 */
bool ApplyExtends(const std::string& path, JsonValue& root,
                  LoadResult& result) {
    const JsonValue* extends = root.Find("$extends");
    if (extends == nullptr) {
        return true;
    }

    const auto fail = [&](const std::string& message) {
        result.errors.push_back(
            CxxcssError{path, extends->where.line, message});
        return false;
    };

    if (!extends->IsString() || extends->text.empty()) {
        return fail("$extends 必须是字符串：相对 CXXCSS 根的路径、**不带 .json**"
                    "（例如 \"Panel/basePanel\"）");
    }
    const std::string rootDir = CxxcssRootOf(path);
    if (rootDir.empty()) {
        return fail("$extends：算不出 CXXCSS 根（这个文件的路径里没有目录）");
    }

    const std::string parentPath = rootDir + "/" + extends->text + ".json";
    if (NormalizeSeparators(parentPath) == NormalizeSeparators(path)) {
        return fail("$extends 指向它自己（\"" + extends->text
                    + "\" 就是本文件）");
    }

    const ParseResult parent = LoadJsonFile(parentPath);
    if (!parent.ok) {
        return fail("$extends 的父文件读不到：" + parentPath
                    + "（路径是相对 CXXCSS 根的、不带 .json）");
    }
    if (parent.value.Find("$extends") != nullptr) {
        return fail("$extends 只支持**一层**：父文件 \"" + extends->text
                    + "\" 自己也写了 $extends。链式继承要环检测、行号归属与"
                      "白名单二次校验，收益不成比例（CXXCSS.md §4.6）");
    }

    root = MergeExtendsObjects(root, parent.value, parentPath);
    return true;
}

void LoadConfigInto(const std::string& path, LoadResult& result) {
    // 第一步：**从目录名定组件类型**（CXXCSS.md §1 第 1 条）。
    // 定不出来就报错，不猜——猜错的后果是一堆莫名其妙的字段报错
    // （拿按钮的字段表去看面板的配置）。
    ComponentKind kind = ComponentKind::Button;
    if (!TryComponentKindFromPath(path, kind)) {
        result.errors.push_back(CxxcssError{
            path, 0,
            "认不出这个配置属于哪种组件：**目录名决定组件类型**"
            "（CXXCSS.md §1 第 1 条），认得的写法是 Button / buttons、"
            "Panel / panels、Label / labels、Image / images（静态图片）、"
            "Text / texts（语言表）、Theme / themes（主题表）、"
            "Config / configs（全局配置，也可以直接放在 CXXCSS/ 根下）；"
            "这个文件的目录名是 \""
                + LeafDirectory(path) + "\""});
        return;
    }

    const ParseResult parsed = LoadJsonFile(path);
    if (!parsed.ok) {
        // parsed.error 已经带了文件名（LoadJsonFile 补的），所以这里不再补，
        // 用空 file 让 Format() 只输出"文件: 行: 说明"里的说明部分。
        result.errors.push_back(CxxcssError{std::string(), 0, parsed.error});
        return;
    }

    // `$extends`（T-10）：**先合并、再交给各 Loader**。反过来的话父文件里
    // 写错的字段会被子文件覆盖掉，永远查不出来。
    JsonValue root = parsed.value;
    if (!ApplyExtends(path, root, result)) {
        return;
    }

    switch (kind) {
        case ComponentKind::Panel: {
            PanelLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Label: {
            LabelLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Image: {
            ImageLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Text: {
            TextLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Theme: {
            ThemeLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Config: {
            ConfigLoader loader(path, result);
            loader.Run(root);
            break;
        }
        case ComponentKind::Button:
        default: {
            ButtonLoader loader(path, result);
            loader.Run(root);
            break;
        }
    }
}

LoadResult LoadConfigs(const std::vector<std::string>& paths) {
    LoadResult result;

    std::vector<std::string> sorted = paths;
    // 排序：生成出来的代码顺序稳定，构建可复现（文件系统的顺序不该影响产物）。
    std::sort(sorted.begin(), sorted.end());

    // 过滤掉示例（CXXCSS.md §1 第 3 条），后面的两趟都看这一份名单。
    std::vector<std::string> real;
    real.reserve(sorted.size());
    for (const std::string& path : sorted) {
        const std::string stem = FileStem(path);
        const std::string suffix = ".example";
        const bool isExample = stem.size() > suffix.size()
                            && stem.compare(stem.size() - suffix.size(),
                                            suffix.size(), suffix) == 0;
        if (!isExample) {
            real.push_back(path);
        }
    }

    // **两趟**：主题表必须先装好。
    //
    // 组件里的 `"$surface"` 在**加载那一刻**就要换成具体值（B8），所以主题不
    // 能等——而文件名排序里 `Theme/` 排在 `Button/` 之后，一趟扫必然来不及。
    // 先收主题、再收组件，`$token` 才有表可查。
    for (const std::string& path : real) {
        ComponentKind kind = ComponentKind::Button;
        if (TryComponentKindFromPath(path, kind)
            && kind == ComponentKind::Theme) {
            LoadConfigInto(path, result);
        }
    }
    for (const std::string& path : real) {
        ComponentKind kind = ComponentKind::Button;
        if (TryComponentKindFromPath(path, kind)
            && kind == ComponentKind::Theme) {
            continue;  // 第一趟已经收过
        }
        // 逐个往**同一个** result 上追加：重名检查要看到前面所有文件的认领
        // （而且是**跨组件类型**的——名字是运行期的全局查找键）。
        LoadConfigInto(path, result);
    }

    // ---- 主题表的收尾：定"烘焙主题" + 跨表校验 ----
    //
    // 多份主题时 `$token` 按**烘焙主题**解析成字面量（产物里的默认值）。
    // 标记 `"default": true` 的那份就是它；没标就按文件名排序取第一份
    // （文件已经排过序，所以是确定的）。
    if (result.themes.size() > 1) {
        std::size_t marked = result.themes.size();
        for (std::size_t i = 0; i < result.themes.size(); ++i) {
            if (!result.themes[i].isDefault) {
                continue;
            }
            if (marked != result.themes.size()) {
                result.errors.push_back(CxxcssError{
                    result.themes[i].filePath, 0,
                    "有两份主题都写了 \"default\": true——烘焙主题只能有一个"});
                break;
            }
            marked = i;
        }
        if (marked < result.themes.size() && marked != 0) {
            std::rotate(result.themes.begin(),
                        result.themes.begin()
                            + static_cast<std::ptrdiff_t>(marked),
                        result.themes.end());
        }
        if (marked == result.themes.size()) {
            // 没标 default：按文件名排序取第一份。这是**确定的**，但很容易
            // 出乎意料（改个文件名就换了默认配色），所以提示一句。
            result.themes.front().warnings.push_back(
                "有 " + std::to_string(result.themes.size())
                + " 份主题但都没写 \"default\": true——烘焙主题（$token 按它烘成"
                  "字面量）按文件名排序取了 \""
                + result.themes.front().name
                + "\"。想固定下来就给那份加一行 \"default\": true");
        }
    }

    // 每张主题表都必须定义组件用到的**每一个**颜色 token。
    //
    // 少一个的表现是"切到那张主题后，这一处还是旧色"——生成期能抓，就别留到
    // 运行期（那边查不到只会用兜底色，不报错）。
    for (const ThemeConfig& theme : result.themes) {
        for (const std::string& token : result.usedColorTokens) {
            if (theme.FindColor(token) == nullptr) {
                result.errors.push_back(CxxcssError{
                    theme.filePath, 0,
                    "主题 \"" + theme.name + "\" 里没有颜色 token \"$" + token
                        + "\"，但组件用到了它（多份主题必须覆盖同一套 token，"
                          "否则切过去那一处还是旧色）"});
            }
        }
    }

    return result;
}

}  // namespace inkgen
