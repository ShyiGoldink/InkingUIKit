# 控件拖拽：源 / 落点 / 幽灵（1.4）

> 覆盖的头文件：`drag/DragSpec.h`（纯数据）、`drag/InkingDragSession.h`（会话）、
> 以及 `widget/InkingWidget.h` 上的 `SetDragSource` / `SetDropTarget` /
> `IsDragOver` 与 `scene/InkingScene.h` 上的七个门面。
>
> 需求与逐条定稿：`GoldingCore/docs/TASK-SDK-DRAGGER.md`。
> 实现：`src/drag/`（两个文件）。探针：`examples/probe_drag/`（A1~A8）。

---

## 什么时候用它

**把一块东西拖到另一块身上，然后发生一件事。** 典型：资源行 → 目录行完成分组、
图层项 → 另一个图层、棋盘上的棋子 → 另一格。

不要用它做：拖动窗口（`MenuBar` 自带）、拖动一个值（`Slider` 自带）、拖动浮窗
（改动态载体的 offset 即可）、文件拖进窗口（那是 `InkingScene::filesDropped`，
操作系统发起的，与本节无关）。

---

## 一句话接上

```cpp
// 源：这一行能被拖，拖的是资源 id
InkingDragSourceSpec src;
src.payloadType = "golding.resource";   // 空 = 不可拖
src.payload     = resourceId;           // void*，**生命周期归你**
src.ghostText   = resourceName;         // 幽灵上那句字（可空）
row->SetDragSource(src);

// 落点：目录行收这一种
InkingDropTargetSpec dst;
dst.acceptType = "golding.resource";    // 必须与上面逐字节相等
dst.onDrop     = [](void* user, void* payload, float x, float y) {
    MoveResourceInto(user, payload);     // 收尾之后才调，删节点 / 重建列表都安全
};
dst.user       = this;
folderRow->SetDropTarget(dst);
```

清除：`SetDragSource({})` / `SetDropTarget({})`（`payloadType` / `acceptType`
为空的声明等于没声明）。

---

## 三张表

### 声明面（`InkingWidget`）

| 方法 | 说明 |
| --- | --- |
| `SetDragSource(spec)` | 声明"我能被拖、拖的是什么"。空 `payloadType` = 清除 |
| `SetDropTarget(spec)` | 声明"我收、收的时候给落点"。空 `acceptType` = 清除 |
| `GetDragSource()` / `GetDropTarget()` | 只读回看不带 ⚠ 的那一份 |
| `IsDragOver()` | 我正被当作落点高亮吗（**状态**，外观自己画） |
| `MarkDragOver(bool)` | 框架调。别在业务里用 |

### 场景面（`InkingScene`）

| 方法 | 说明 |
| --- | --- |
| `IsDragging()` | 真的拖起来了吗（已逾 4px 阈值、幽灵在） |
| `GetDragSourceNode()` | 正在被拖的控件；没在拖 = `nullptr` |
| `GetDragTargetNode()` | 当前高亮的落点 |
| `GetDragGhostNode()` | 幽灵节点（场景拥有；只给自检 / 诊断） |
| `GetDragSession()` | 会话只读视图（落靶 / 取消计数） |
| `BeginDrag(src)` | 程序化起拖（键盘 / 脚本 / 自检）。已在拖等四种情况返回 false |
| `CancelDrag()` | 显式取消，顺手放掉主键捕获 |

### 常量（`DragSpec.h`）

| 常量 | 值 | 为什么是这个数 |
| --- | --- | --- |
| `InkingDragStartThreshold` | 4 px | 没有阈值按钮就点不下去（手一定会抖） |
| `InkingDragGhostZIndex` | 850 | 高于弹窗 400 / 菜单 260，低于光标 900 |

---

## 五条手感（写死的，不是配置）

1. **4px 起拖阈值**。没过阈值就松手 = 普通点击，`onClicked` 照常触发。
2. **拖拽是独占会话**。这次松手不派发 click（哪怕指针又滑回源身上），
   期间 hover 不传给别的控件。
3. **幽灵挂在场景下、z = 850、恒不参与命中**。所以指针底下的答案永远是
   "要放到哪儿"，不是"跟着跑的那块板"。
4. **取消四条路，一处收尾**：Esc、松手在无落点处、源控件被移除、`CancelDrag()`。
5. **一次只允许一个拖拽**。多选批量拖是明确不做的（payload 是单值，
   多值要定义"部分失败"的语义，属于另一层）。

### 取消与落靶的区别

| | `onDrop` | 幽灵 | 源的行为 |
| --- | --- | --- | --- |
| 落靶 | 调一次，参数 `(payload, x, y)` | 删除 | 收到 `press(false)`，回到常态 |
| 落空 / Esc / 源被删 | **不调** | 删除 | 同上 |
| `CancelDrag()` | **不调** | 删除 | 同上，并**当场**放掉主键捕获 |

`onDrop` 在**收尾之后**才调：回调里删节点、重建整张列表、连 payload 一起释放都
安全。代价是那时 `GetDragSourceNode()` 已经是 `nullptr`——要源的信息请塞进
`payload`。

---

## 三个边界

- **payload 归调用方**：库原样转交 `void*`，不解引用、不 clone、不释放。
  源控件在拖拽途中被删时库**只清指针、不访问**（探针 A6 钉的就是它）。
- **类型标签按内容比较**（`strcmp`）。写字符串字面量最省心；动态拼的串必须
  活得比这组 spec 长。
- **落点按最近祖先解析**：指针下命中的是行里的标签，标签没声明落点就沿父链往上找
  ——第一个 `acceptType` 匹配的祖先算落点。类型不匹配的祖先**会被跳过**
  （所以"行只收图片、外层列表收一切"是有效配置）；类型对但 `canDrop` 说不 ⇒
  就是这里不行，不再往上找。

---

## 高亮怎么画（库不管外观）

```cpp
void Row::onRender(float pixelX, float pixelY) const {
    const std::uint32_t fill = IsDragOver() ? kDropFill : kNormalFill;
    // ... 按 fill 画自己的底
}
```

与 `MouseHover()` 同一条分工：库只回答状态，外观是消费方的事。
`IsDragOver()` 变化时标的是**重绘脏**（外观变了，命中表一个字节都没动）——
标成命中脏的话，拖过一串落点会每帧白重建一次绘制列表。

---

## 明确不做

列表边缘自动滚动、跨窗口 / 跨进程拖拽、多选批量拖、排序 / 落位动画、payload
生命周期管理；`MenuBar` 的窗口拖动与 `Slider` 的拖值也不改造（那是"控件内部
状态"，与跨组件拖放不是同一件事）。

---

## 验收（探针 A1~A8）

`ctest --test-dir build -R probe_drag`（约 0.4 s，退出码 = 失败项数）：

| # | 场景 | 断言 |
| --- | --- | --- |
| A1 | 按下 → 移 2px → 抬起 | 不算拖；`onClicked` 一次 |
| A2 | 按下 → 移 10px | 幽灵出现、挂在场景下、z=850、不吃命中；源进入被拖状态 |
| A3 | 移到落点抬起 | `onDrop` 一次、payload 与落点坐标对；幽灵消失；无 click |
| A3b | 拖出去再拖回源身上松开 | 仍然没有 click（独占会话） |
| A4 | 移到空白处抬起 | 不调 `onDrop`；幽灵消失；无 click |
| A5 | 拖拽中按 Esc | 幽灵消失；`onDrop` 不调；源回常态；之后松手无 click |
| A6 | 拖拽中摘掉源节点 | 不崩；幽灵消失；状态清零 |
| A7 | 落点类型不匹配 | 不高亮、不调 `onDrop` |
| A8 | 拖拽中第二个源起拖 | 被拒绝；仍然只有一只幽灵 |
