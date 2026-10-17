#include <detail/InkingHit.h>

#include <dataStruct/InkingAffine.h>

// `AncestorAffine` 与 `GetAbsX/Y` 都在 Widget 层，所以这里要完整定义——
// 头文件里对它们只有前向声明（见 `include/detail/InkingHit.h` 顶上那段）。
#include <detail/InkingGeometry.h>
#include <widget/InkingWidget.h>

namespace inking::detail {

bool HitTestShape(const InkingWidget& node, const ShapeSpec& shape,
                  const TransformSpec& transform, float worldX, float worldY) {
    // 父链上任何一层不可见，就不该还能被点到。
    if (!node.IsVisibleInTree()) {
        return false;
    }

    // ① 撤掉祖先子树变换：容器动过之后，世界上那个点要回到"未经变换的布局
    //    空间"里才能和自己的绝对位置相减。绘制那边在提交层过的是正变换
    //    （`SubmitToRenderer` 里的 `AncestorAffine`），两边同源。
    Point2 world{worldX, worldY};
    const Affine ancestor = AncestorAffine(node);
    if (!ancestor.IsIdentity()) {
        world = ApplyAffine(InvertAffine(ancestor), worldX, worldY);
    }
    const float localX = world.x - node.GetAbsX();
    const float localY = world.y - node.GetAbsY();

    // ② 变换通道：过一次**逆**变换（绘制过正变换，见 `fillShape`）。
    //    中心取组件中心，和绘制用的是同一个原点。
    const Point2 local = InverseTransform(
        transform, localX, localY,
        static_cast<float>(node.GetWidth()) * 0.5f,
        static_cast<float>(node.GetHeight()) * 0.5f);

    // ③ 形状是唯一真相：渲染、命中、AABB 都从 GetShape() 派生。
    return ShapeContains(shape, local.x, local.y, node.GetWidth(),
                         node.GetHeight());
}

bool HitTestLocalRect(const InkingWidget& node, const ShapeBounds& bounds,
                      const TransformSpec& transform, float worldX,
                      float worldY) {
    if (!node.IsVisibleInTree()) {
        return false;
    }

    // 前三步与 `HitTestShape` 完全一样（同一份变换链，见那里的注释）——
    // 两份实现各自演化就会出现"形状剪对了、裁剪区剪错了"这种只在角落露头的错。
    Point2 world{worldX, worldY};
    const Affine ancestor = AncestorAffine(node);
    if (!ancestor.IsIdentity()) {
        world = ApplyAffine(InvertAffine(ancestor), worldX, worldY);
    }
    const float localX = world.x - node.GetAbsX();
    const float localY = world.y - node.GetAbsY();

    const Point2 local = InverseTransform(
        transform, localX, localY,
        static_cast<float>(node.GetWidth()) * 0.5f,
        static_cast<float>(node.GetHeight()) * 0.5f);

    // 矩形包含（边界算在内）。不用 `ShapeContains`：那一条吃的是"节点本体的
    // 尺寸"，而裁剪区可以是任意一块矩形。
    return local.x >= bounds.x && local.x <= bounds.x + bounds.width
        && local.y >= bounds.y && local.y <= bounds.y + bounds.height;
}

}  // namespace inking::detail
