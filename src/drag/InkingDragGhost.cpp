// 拖拽幽灵（**内部实现**，不在公开面）。
//
// 任务书 T-03 要的就是这一块：一个跟着指针走、吃不到命中、压在弹窗之上光标之下的
// 板子。三件事决定它长成这样：
//
//   1. **它是场景的直接子节点，z = 850**（`InkingDragGhostZIndex`）。
//      不是"画在场景之上的一笔"——`onRenderScene` 那一笔整体在子孙之下，
//      拿来画幽灵就会被任何弹窗盖住。层序由构造保证（挂 scene + 定 z），
//      不靠"记得最后画"（`docs/AGENTS.md` 那条"层序要由构造保证"）。
//
//   2. **它恒不参与命中**（`HitTest` 恒 false）。拖拽期间指针底下的答案必须
//      是"我要放到哪儿"，而不是跟着指针跑的那块板。动态档的洞因此也不会把
//      指针"骗"进来（`QueryHit` 问动态层时它照样答"不命中"）。
//
//   3. **外观是本文件的事，业务不参与**。库不认识"高亮 / 选中"长什么样，但
//      幽灵不是控件状态——它只是"你正拖着什么"的临时提示，样式固定下来
//      反而不会出现"十个插件十种幽灵"。
//
// 为什么是 `.cpp` 里的匿名类型：它没有公开的构造入口、没有业务能调的写方法，
// 唯一的生产者是 `inking_detail::CreateDragGhost`。放进公开头等于请人来
// `dynamic_cast` 它（本仓最反对的那件事）。

#include <drag/DragSpec.h>
#include <drag/InkingDragSession.h>

#include <config/window_config.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>
#include <scene/InkingScene.h>
#include <widget/InkingDynamicWidget.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

/// 幽灵的外观（设计坐标 / 0xAARRGGBB）。集中在这里，改观感只改这一处。
struct GhostLook {
    static constexpr float radius = 6.0f;
    static constexpr float paddingX = 10.0f;
    static constexpr float paddingY = 6.0f;
    static constexpr float fontSize = 13.0f;
    static constexpr std::uint32_t fill = 0xE02A2F38u;    ///< 深色半透明底
    static constexpr std::uint32_t border = 0x703C4250u;  ///< 一像素描边
    static constexpr std::uint32_t text = 0xFFEDEFF2u;    ///< 近白字
    /// 指针与幽灵左上角的错位：让幽灵**不压在指针正下方**（否则指针尖
    /// 正好落在幽灵里，用户看不见自己指着哪儿）。
    static constexpr float pointerGapX = 12.0f;
    static constexpr float pointerGapY = 12.0f;
};

/// 按文案量一次尺寸（量不出来返回 0×0，调用方退回 1×1）。
InkingTextMetrics MeasureGhostText(const std::string& text) {
    if (text.empty()) {
        return InkingTextMetrics{};
    }
    InkingTextRequest request;
    request.text = text;
    request.fontSize = GhostLook::fontSize;
    // 用**预制件那一档的默认字体**（资源圆体，带中文），理由同 `Tooltip`：
    // 重复写一份候选表就是第二份真相。头文件不依赖 prefab，层级没有倒退。
    request.fontPath = inking_prefab::theme::DefaultFontPath();
    return InkingMeasureText(request);
}

class InkingDragGhost final : public InkingDynamicWidget {
public:
    InkingDragGhost(InkingWidget* parent, int width, int height,
                    std::string text)
        : InkingDynamicWidget(parent, MakeData(width, height)),
          _text(std::move(text)) {
        SetName("$inking.drag.ghost");
    }

    /// 恒不命中（见文件头第 2 条）。`noexcept` 不必：基类没写。
    bool HitTest(float /*worldX*/, float /*worldY*/) const override {
        return false;
    }

    /// 光标形状也不该被它改：拖拽期间指针的形状由窗口层按"正在拖"自己定。
    InkingCursorShape GetCursorShape() const noexcept override {
        return InkingCursorShape::Default;
    }

    /// 跟手：`selfAnchor` / `traceAnchor` 都是左上角，所以偏移就是绝对位置
    /// （幽灵直接挂在场景下，场景原点就是设计坐标原点）。
    void MoveTo(float x, float y) {
        // 夹在设计画布里：拖到窗口右边缘时幽灵整个飘出去，看着像"拖丢了"。
        const float limitX =
            static_cast<float>(InkingDesignWidth()) - static_cast<float>(GetWidth());
        const float limitY =
            static_cast<float>(InkingDesignHeight()) - static_cast<float>(GetHeight());
        ChangeOffset(std::clamp(x, 0.0f, std::max(0.0f, limitX)),
                     std::clamp(y, 0.0f, std::max(0.0f, limitY)));
    }

protected:
    void onRender(float pixelX, float pixelY) const override {
        SDL_Renderer* renderer = inking_detail::currentRenderer();
        if (renderer == nullptr) {
            return;
        }
        const float magnification =
            GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        const InkingShapeSpec shape = InkingShapeSpec::RoundedRect(GhostLook::radius);
        const InkingTransformSpec transform{};  // 幽灵不转也不缩

        inking_detail::fillShape(renderer, shape, GetWidth(), GetHeight(),
                                 magnification, pixelX, pixelY, transform,
                                 GhostLook::fill);
        inking_detail::strokeShape(renderer, shape, GetWidth(), GetHeight(),
                                   magnification, pixelX, pixelY, transform, 1.0f,
                                   GhostLook::border);
        paintText(renderer, pixelX, pixelY, magnification, transform);
    }

private:
    /// 幽灵的静态数据：尺寸 + z + 颜色。三个都定死，业务无从改。
    static InkingAnchorData MakeData(int width, int height) {
        InkingAnchorData data;
        data.width = std::max(1, width);
        data.height = std::max(1, height);
        // 左上角对齐：`GetX/Y` 就等于 `offset`，跟手不用再解锚点公式。
        data.selfAnchor = InkingAnchorPoint{0.0f, 0.0f};
        data.traceAnchor = InkingAnchorPoint{0.0f, 0.0f};
        data.zIndex = InkingDragGhostZIndex;
        data.color = GhostLook::fill;
        return data;
    }

    void paintText(SDL_Renderer* renderer, float pixelX, float pixelY,
                   float magnification,
                   const InkingTransformSpec& transform) const {
        if (_text.empty()) {
            return;
        }
        InkingTextRequest request;
        request.text = _text;
        request.fontSize = GhostLook::fontSize;
        request.fontPath = inking_prefab::theme::DefaultFontPath();
        request.color = GhostLook::text;
        // 居中：幽灵的尺寸可能是源控件的尺寸（比文字宽得多），左对齐会看着像
        // "文字漏在一边"。
        const InkingTextMetrics metrics = InkingMeasureText(request);
        request.offsetX =
            std::max(0.0f, (static_cast<float>(GetWidth()) - metrics.width) * 0.5f);
        request.offsetY =
            std::max(0.0f, (static_cast<float>(GetHeight()) - metrics.height) * 0.5f);
        inking_detail::PaintText(renderer, request, GetWidth(), GetHeight(),
                                 magnification, pixelX, pixelY, transform);
    }

    std::string _text;
};

}  // namespace

namespace inking_detail {

InkingWidget* CreateDragGhost(InkingScene& scene,
                              const InkingDragSourceSpec& spec, int width,
                              int height) {
    const std::string text =
        spec.ghostText != nullptr ? std::string(spec.ghostText) : std::string();

    int ghostWidth = width;
    int ghostHeight = height;
    if (ghostWidth <= 0 || ghostHeight <= 0) {
        // 没给尺寸（也没从源控件问到）：按文案量。**必须**兜成至少 1×1——
        // 0 尺寸的节点不进命中表、也不画，幽灵会"存在但看不见"。
        const InkingTextMetrics metrics = MeasureGhostText(text);
        if (ghostWidth <= 0) {
            ghostWidth = static_cast<int>(std::ceil(
                metrics.width + GhostLook::paddingX * 2.0f));
        }
        if (ghostHeight <= 0) {
            ghostHeight = static_cast<int>(std::ceil(
                metrics.height + GhostLook::paddingY * 2.0f));
        }
        ghostWidth = std::max(1, ghostWidth);
        ghostHeight = std::max(1, ghostHeight);
    }

    auto ghost = std::make_unique<InkingDragGhost>(&scene, ghostWidth, ghostHeight,
                                                   text);
    // 场景**拥有**它：会话只记裸指针，摘掉时走 `RemoveChild`（`end()` 里那一步）。
    return scene.AddChild(std::move(ghost));
}

void MoveDragGhost(InkingWidget* ghost, float pointerX, float pointerY) {
    if (ghost == nullptr) {
        return;
    }
    // 这里是**唯一**知道"幽灵就是 `InkingDragGhost`"的地方（它是本文件造的）。
    // 静态转换而不是 dynamic_cast：类型由 `CreateDragGhost` 保证，多一次运行期
    // 查询只是给"本仓最反对的那种类型发现"留一扇门。
    static_cast<InkingDragGhost*>(ghost)->MoveTo(
        pointerX + GhostLook::pointerGapX, pointerY + GhostLook::pointerGapY);
}

}  // namespace inking_detail
