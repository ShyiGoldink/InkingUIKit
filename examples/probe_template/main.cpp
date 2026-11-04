// ---------------------------------------------------------------------------
// inking_probe_template —— **单个控件的探针模板**（复制这一个文件就能开工）
//
// 什么时候用它，什么时候改 `demo/selftest.cpp`
//   - `selftest.cpp`   验"库作为 DLL、从消费者视角能不能用"：跨模块的单例、
//                      导出符号、一条链上有好几个组件的事。它已经 9800 行，
//                      再加细账会让"红了跑哪一条"变成考古。
//   - 本模板           只验**一个控件**的全部细节。一次构建 + 一次运行 < 1 秒，
//                      红了就只可能是这一件事。
//
// 复制之后要改**五处**（下面都打了 `TODO(复制后改)`）：
//   1. 场景名 —— 必须全局唯一。撞名会被 `InkingSceneLibrary` 驳回，
//      而驳回之后场景是"停放态"：探针全绿、什么都没验（最难发现的假绿）。
//   2. 建组件 —— 你要测的那个，给固定几何与固定颜色（别用主题默认值：
//      主题一改，像素断言就跟着红）。
//   3. ① 状态断言 —— 纯数据，不碰渲染器，任何后端都能跑。
//   4. ② 事件断言 —— 订阅槽 → 触发 → 数次数、看参数。
//   5. ③ 像素断言 —— 有渲染器才跑，没有就 `[skip]`。
//
// 三条贯穿全篇的规矩
//   - **退出码 = 失败项数**（0 = 全绿）。所以 `add_test` 什么都不用配，
//     也不要再写 `FAIL_REGULAR_EXPRESSION`（改文案时会误红）。
//   - **断言要能真的红**。写之前先想："我要是把实现改反了，这条会不会红？"
//     不会红的断言等于注释。像素断言尤其如此——先确认两种颜色真的不同。
//   - **组件落在哪，问它自己**（`GetAbsX/Y`）。别拿配置里的 `offsetX` 猜：
//     锚点、父级排版、滚动都会挪它。
//
// 跑法
//   ./bin/inking_probe_template                        # dummy 后端（默认）
//   INKING_PROBE_DRIVER=windows ./bin/inking_probe_template
//   建不出渲染器时 `return 77`（ctest 的 SKIP_RETURN_CODE ⇒ 显示 Skipped，
//   与"代码坏了"区分开）。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <config/window_config.h>
#include <prefab/Slider.h>

#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fail = 0;

/// 断言 + 打一行。**没有宏**：调用点在栈里直接可见，红的时候能下断点。
void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) {
        ++g_fail;
    }
}

/// 读回一块像素（ARGB8888，逐行拷贝）。
///
/// 注意 `SDL_RenderReadPixels` 读的是**当前后缓冲**：必须在 `RenderScene` 之后、
/// present 之前读。先 `SDL_ConvertSurface` 到 ARGB8888 再取，别假设后端原生格式
/// （软件后端是 ARGB8888，GPU 未必）。
std::vector<std::uint32_t> ReadPixelRegion(SDL_Renderer* renderer,
                                          const SDL_Rect& rect) {
    std::vector<std::uint32_t> out;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, &rect);
    if (shot == nullptr) {
        return out;
    }
    SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(shot);
    if (argb == nullptr || !SDL_LockSurface(argb)) {
        SDL_DestroySurface(argb);
        return out;
    }
    out.resize(static_cast<std::size_t>(rect.w) * static_cast<std::size_t>(rect.h));
    for (int row = 0; row < rect.h; ++row) {
        std::memcpy(&out[static_cast<std::size_t>(row) * rect.w],
                    static_cast<const std::uint8_t*>(argb->pixels) +
                        static_cast<std::size_t>(row) * argb->pitch,
                    sizeof(std::uint32_t) * static_cast<std::size_t>(rect.w));
    }
    SDL_UnlockSurface(argb);
    SDL_DestroySurface(argb);
    return out;
}

/// 通道差 ≤ tolerance。**不写逐位相等**：真后端有混合 / 色彩空间，差 1~2 是常态；
/// 而"两个部件颜色不同"这种断言，容差取 8 完全够区分（默认配色差着几十）。
bool NearColor(std::uint32_t a, std::uint32_t b, int tolerance = 8) {
    for (int shift = 0; shift <= 16; shift += 8) {
        const int left = static_cast<int>((a >> shift) & 0xFFu);
        const int right = static_cast<int>((b >> shift) & 0xFFu);
        const int delta = left > right ? left - right : right - left;
        if (delta > tolerance) {
            return false;
        }
    }
    return true;
}

/// TODO(复制后改) 1/5：**场景名必须唯一**。
struct TemplateScene : public InkingScene {
    TemplateScene() : InkingScene("probe_template") {}
};

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("== 探针模板：滑块（复制这个文件，改五处）==\n");

    // ---- 后端：默认 dummy（任何机器都能跑）；真后端按需开 ----
    //
    // `SDL_SetHint` 必须在 `SDL_Init` **之前**：hint 只在建驱动时被消费，
    // 晚一步就是静默无效（设了、没生效、没报错）。
    const char* driverEnv = std::getenv("INKING_PROBE_DRIVER");
    const std::string driver =
        (driverEnv != nullptr && driverEnv[0] != '\0') ? driverEnv : "dummy";
    std::printf("   视频驱动 = %s\n", driver.c_str());

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver.c_str());
    Check(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
    SDL_Window* window = SDL_CreateWindow("ink-probe", 640, 360, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;

    // 真后端建不出来 = 这台机器没显卡驱动，不是代码坏了 ⇒ 77（ctest 显示 Skipped）。
    if ((window == nullptr || renderer == nullptr) && driver != "dummy") {
        std::printf("[skip] 真后端不可用：%s\n",
                    SDL_GetError()[0] != '\0' ? SDL_GetError() : "（无错误信息）");
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return 77;
    }
    Check(renderer != nullptr, "创建渲染器（dummy 下是软件后端）");

    // ---- 场景 ----
    TemplateScene scene;
    if (!scene.IsRegistered()) {
        // 撞名时**必须红**：不红的话后面所有断言都在跑一个停放态的场景。
        Check(false, "① 探针场景注册成功（名字唯一）");
        std::printf("失败项：%d\n", g_fail);
        return g_fail;
    }

    // ---- TODO(复制后改) 2/5：建组件 ----
    // 固定几何 + 固定颜色：主题换了、配色改了，这里的断言不该跟着红。
    inking_prefab::SliderData data = inking_prefab::SliderData::MakeDefault();
    data.name = "tpl_slider";
    data.width = 200;
    data.height = 28;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    data.min = 0.0f;
    data.max = 100.0f;
    data.value = 0.0f;
    data.step = 10.0f;  // 量化，顺带验"step 真的生效"
    data.track = InkingButtonAppearance(InkingAppearanceType::Color, 0xFF303030u);
    data.fill = InkingButtonAppearance(InkingAppearanceType::Color, 0xFFE040A0u);
    data.thumb = InkingButtonAppearance(InkingAppearanceType::Color, 0xFFFFFFFFu);

    auto* slider = static_cast<inking_prefab::Slider*>(
        scene.AddChild(std::make_unique<inking_prefab::Slider>(&scene, data)));
    Check(slider != nullptr, "② 组件挂进场景");

    // ---- TODO(复制后改) 3/5：状态断言 ----
    std::printf("\n-- ① 状态：夹取 / 量化 / 幂等 --\n");
    Check(std::fabs(slider->GetValue() - 0.0f) <= 0.001f,
          "初始值 = 构造时给的 value");
    Check(slider->SetValue(30.0f) && std::fabs(slider->GetValue() - 30.0f) <= 0.001f,
          "SetValue(30) 生效");
    Check(!slider->SetValue(30.0f),
          "同值再来一次返回 false（每帧喂同一个值 ⇒ 零额外重绘）");
    slider->SetValue(34.0f);
    Check(std::fabs(slider->GetValue() - 30.0f) <= 0.001f,
          "step=10 ⇒ 34 量化到 30（值域不是连续数轴）");
    slider->SetValue(1000.0f);
    Check(std::fabs(slider->GetValue() - 100.0f) <= 0.001f, "1000 → 夹到 max");
    slider->SetValue(-1000.0f);
    Check(std::fabs(slider->GetValue() - 0.0f) <= 0.001f, "-1000 → 夹到 min");
    Check(slider->GetWidth() == 200 && slider->GetHeight() == 28,
          "改值不改组件尺寸（容器不重排）");

    // ---- TODO(复制后改) 4/5：事件断言 ----
    std::printf("\n-- ② 事件：发几次、参数对不对 --\n");
    int changedCount = 0;
    float lastNow = -1.0f;
    float lastBefore = -1.0f;
    slider->changed.Subscribe([&](float now, float before) {
        ++changedCount;
        lastNow = now;
        lastBefore = before;
    });
    slider->SetValue(50.0f);   // 0 → 50：发
    slider->SetValue(50.0f);   // 同值：不发
    slider->SetValue(70.0f);   // 50 → 70：发
    Check(changedCount == 2, "两次真变化 ⇒ changed 恰好 2 次（同值那次不算）");
    Check(std::fabs(lastNow - 70.0f) <= 0.001f &&
              std::fabs(lastBefore - 50.0f) <= 0.001f,
          "参数是 (现在, 之前)，顺序没反");

    // ---- TODO(复制后改) 5/5：像素断言 ----
    std::printf("\n-- ③ 像素：画出来的和部件几何一致吗 --\n");
    if (renderer == nullptr) {
        std::printf("      [skip] 没有渲染器——像素断言不适用\n");
        std::printf("\n失败项：%d\n", g_fail);
        return g_fail;
    }

    slider->SetValue(30.0f);  // 填充占 30%，两侧取样点都离滑块圆盘够远
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);  // 读像素前必须真画一帧

    const inking_prefab::SliderParts parts = slider->GetParts();
    const int absX = static_cast<int>(std::lround(slider->GetAbsX()));
    const int absY = static_cast<int>(std::lround(slider->GetAbsY()));
    const int midY = static_cast<int>(
        std::lround(parts.track.y + parts.track.height * 0.5f));
    const auto sampleAt = [&](float localX) {
        const std::vector<std::uint32_t> one = ReadPixelRegion(
            renderer, SDL_Rect{absX + static_cast<int>(std::lround(localX)),
                               absY + midY, 1, 1});
        return one.empty() ? 0u : one[0];
    };

    const std::uint32_t leftPixel = sampleAt(parts.fill.width * 0.5f);   // 填充段中间
    const std::uint32_t rightPixel = sampleAt(parts.track.width * 0.9f); // 未填充段
    std::printf("     组件框绝对位置 (%d, %d)；填充宽 %.1f / 轨道宽 %.1f\n", absX,
                absY, static_cast<double>(parts.fill.width),
                static_cast<double>(parts.track.width));
    std::printf("     左半 0x%08X / 右半 0x%08X\n", leftPixel, rightPixel);

    Check(NearColor(leftPixel, data.fill.color), "左半 = 填充色");
    Check(NearColor(rightPixel, data.track.color), "右半 = 轨道色");
    // 这一条是"上一条不会假绿"的保险：两组颜色若刚好一样，上面两条都会过。
    Check(!NearColor(leftPixel, rightPixel),
          "左右两半颜色确实不同（否则上面两条断言没有区分力）");

    // ---- 收尾 ----
    std::printf("\n失败项：%d\n", g_fail);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return g_fail;  // 退出码 = 失败项数
}
