#pragma once

// 按钮库：**名字 → ButtonData** 的登记表。
//
// 它存在的理由只有一个：`InkingStaticButton(parent, "name")` 这条路要有东西可查。
// 代码生成器将来把 CXXCSS/Button/*.json 展开成"往这张表里登记"的代码，
// 于是"按名字构造按钮"和"按 ButtonData 构造按钮"就落在同一个实现上
// （见 InkingStaticButton.cpp），不用写两遍。
//
// 与 SceneLibrary 的**刻意不同**：
//   - 场景是**对象**登记（同名只能有一个活的场景），这里登记的是**配置数据**，
//     谁都不拥有它、也没有生命周期要管；
//   - 因此没有 RAII 令牌，也不需要 Close：登记是一次性的，表建起来就是只读的。
//
// 与 SceneLibrary 的**刻意相同**：
//   - 存储用**函数内静态量**，不用类的静态数据成员。类的静态数据成员在 main
//     之前按翻译单元顺序初始化，而生成器登记的代码可能由某个静态对象的构造
//     触发——那就是静态初始化 / 析构顺序问题（AGENTS §6 第 12 条）。
//     函数内静态量首次使用时才初始化，谁先登记就把库先拉起来。
//   - 同名**驳回**，不覆盖：名字是代码里的查找键，"后注册的悄悄顶掉先注册的"
//     会让"按钮长什么样"取决于初始化顺序。
//
// 不加锁：登记发生在启动期，查找只发生在 UI 线程。

#include <button/ButtonData.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inking {

/**
 * @brief 按钮配置的登记表：名字 → ButtonData。
 *
 * 表里的 ButtonData 建好之后**不再变动**，所以 `Find` 返回的裸指针可以长期持有，
 * 直到进程退出——不需要每帧去查表。
 */
class ButtonLibrary {
public:
    /** 登记结果。失败原因分得清，才能给出有用的日志。 */
    enum class RegisterResult {
        Ok,         /**< 登记成功 */
        NameTaken,  /**< 同名已经登记过——驳回，不覆盖 */
        Invalid,    /**< 名字为空 */
    };

    /**
     * 登记一份按钮配置。
     *
     * @param data 配置；它的 `name` 就是查找键，必须非空。
     * @return 只有 Ok 表示真的进表了。
     */
    static RegisterResult Register(const ButtonData& data);

    /**
     * 按名字查配置；没登记过返回 nullptr。
     *
     * 返回的指针由库持有、内容只读，可以长期保存——不要试图改它。
     */
    static const ButtonData* Find(const std::string& name);

    /** 这个名字登记过吗。 */
    static bool IsNameTaken(const std::string& name);

    /** 所有登记过的名字，主要给自检与日志用。 */
    static std::vector<std::string> GetAllNames();

    /** 表里有多少项。 */
    static std::size_t Count();

    /**
     * 开始一次批量登记（生成器生成的 `RegisterAllButtons()` 用）。
     *
     * 配对使用：`BeginRegistrationBatch()` … `Register(...)` … `EndRegistrationBatch()`。
     * 开始时把**上一批**登记过的名字撤下，所以：
     *   - 同一份生成代码重复运行（自检、运行期重建）不会撞 NameTaken；
     *   - 只撤自己那一批，**不碰**别人登记的内容（自检自己塞的那些照旧在）。
     *
     * 为什么需要它：`ClearForTest` 是给自检的"清空一切"，拿它做重新登记会把
     * 别人的条目一起清掉——那是比重复登记更难查的错。
     */
    static void BeginRegistrationBatch();

    /** 结束一次批量登记。之后再 `Register` 的条目属于新的一批。 */
    static void EndRegistrationBatch();

    /**
     * 清空登记表。**只给自检用**。
     *
     * 库本身是只增不减的：它存在的意义就是"启动期建好、之后只读"。
     * 但自检需要从一张空表开始，否则用例之间会互相踩名字；所以留这一个
     * 出口，并在头文件里写清楚它不是给业务代码用的。
     */
    static void ClearForTest();

private:
    ButtonLibrary() = delete;
    ~ButtonLibrary() = delete;
};

}  // namespace inking
