#pragma once

#include <input/PointerButton.h>

//这里是鼠标输入，如果接收到了鼠标的分发事件，则通过重写钩子来实现对于不同输入的反应
class InkingPointerHandler {
public:
    virtual ~InkingPointerHandler() = default;

    //获取父链上最近的滚轮，交给InkingWidget查询
    virtual bool HandlesSubtreeWheel() const noexcept { return false; }

    // 指针进出事件，hover = true进入，false离开
    void PushPointerHover(bool hover) noexcept { onPointerHover(hover); }

    /*
     按下/抬起事件。press = true 按下，false 抬起
     抬起与按下走同一条（而不是两个入口），回哪一态 / 还算不算一次点击由按钮自己决定，按钮按下滑出去再松开要回Normal，不是Hover
    */
    void PushPointerPress(InkingPointerButton button, bool press) noexcept {
        onPointerPress(button, press);
    }

    //一次完整的点击事件
    void PushPointerClick(InkingPointerButton button, float designX,float designY) noexcept {
        onPointerClick(button, designX, designY);
    }

    /*
     滚轮事件
     归属：指针下最上面的那个静态组件；若这时正按着某个节点（拖动捕获生效中），改推给捕获者不会同时给两个组件。
    */
    void PushPointerWheel(float deltaX, float deltaY) noexcept {
        onPointerWheel(deltaX, deltaY);
    }

    /*
     拖动事件
     只有捕获者收得到：按下时命中的那个节点抓住这次交互，之后指针怎么移都只推给它（跨组件不被抢，移出矩形也算它的）；抬起或窗口失焦时释放。
     */
    void PushPointerDrag(float designX, float designY, float deltaX,
                         float deltaY) noexcept {
        onPointerDrag(designX, designY, deltaX, deltaY);
    }

    // 指针状态改变事件。只对动态节点推
    void PushPointerStateChanged() noexcept { onPointerStateChanged(); }

protected:
    // 指针进出，只表示指针在你身上，不代表按下。
    virtual void onPointerHover(bool /*hover*/) {}

    // 按下 / 抬起（press = false 是抬起），重写了必须判 button。
    virtual void onPointerPress(InkingPointerButton /*button*/, bool /*press*/) {}

    // 一次完整点击，坐标是按下点（设计坐标）。
    virtual void onPointerClick(InkingPointerButton /*button*/, float /*designX*/,float /*designY*/) {}

    // 滚轮，本帧累积量；一次性事件，没收到就是没滚。
    virtual void onPointerWheel(float /*deltaX*/, float /*deltaY*/) {}

    // 拖动，只有捕获者收到；delta 相对上一次拖动。
    virtual void onPointerDrag(float /*designX*/, float /*designY*/, float /*deltaX*/, float /*deltaY*/) {}

    // 指针状态变了，只推给动态节点；不带参数，自己读场景。
    virtual void onPointerStateChanged() {}
};

