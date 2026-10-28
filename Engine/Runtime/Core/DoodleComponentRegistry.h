//
//  DoodleComponentRegistry.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <type_traits>

#include "DoodleComponentConfig.h"
#include "DoodleGameObject.h"

namespace Doodle
{
    /**
     * 内容装配前的参数校验
     *
     * Debug 专用。注册助手会在没有宿主的临时组件上跑一遍 Deserialize，
     * 配置错了在创建任何 GameObject 之前就能报出来。Release 下注册项里是 nullptr。
     *
     * 实现约定：
     * - 只读 config，不创建 GameObject / 组件，不访问场景；
     * - 配置非法时抛异常，异常消息要带 config 的路径；
     * - 不会在运行时被调用，只在内容装配前使用。
     *
     * 用普通函数指针而不是 std::function：注册项是启动期一次性写入、随进程存在，
     * 不需要捕获状态；函数指针没有分配，也不会出现捕获对象悬空。
     */
    using DoodleComponentValidateFn = void (*)(const DoodleComponentConfig& config);

    /**
     * 真正创建组件
     *
     * 在所有 GameObject 都建好之后调用。
     *
     * 实现约定：
     * - 通过 GameObject::AddComponentFromConfig 创建，让 Deserialize 在 OnAwake 之前生效；
     * - 此时所有 GameObject 已存在，但排在后面的自定义组件可能还没创建；
     * - 可以抛异常；参数错误最好已经在 validate 阶段被拦住。
     */
    using DoodleComponentCreateFn = void (*)(DoodleGameObject& gameObject,
                                             const DoodleComponentConfig& config);

    /**
     * 一个可序列化组件的注册项
     *
     * name 是内容里的稳定 ID：场景 JSON 里的组件 type 和它逐字对应，类可以
     * 改名，这个名字不能随便改。它承担 Unity MonoBehaviour GUID 的角色。
     */
    struct DoodleComponentRegistration
    {
        /**
         * 组件类型名，也是注册表的 key
         *
         * 必须是静态生命周期字符串（通常直接传字面量）：map 的 key 会另存一份，
         * 但本结构里的 string_view 仍指向调用方传入的原字符串。
         */
        std::string_view name;

        /**
         * 装配前的参数校验；nullptr 表示不做校验
         *
         * Debug 下由注册助手生成；Release 下恒为 nullptr。
         */
        DoodleComponentValidateFn validate = nullptr;

        /**
         * 创建组件；必须非空，Register 会拒绝空值
         */
        DoodleComponentCreateFn create = nullptr;
    };

    /**
     * 组件类型注册表：类型名 → 注册项
     *
     * 引擎只认识类型名和函数指针，不认识任何 GamePlay 类型。GamePlay 与
     * Engine/Package 在启动时把类型登记进来，内容加载时按名字查表。
     *
     * 登记入口是静态方法 RegisterComponent<T>；本类负责保存和查找注册项。
     *
     * 注册必须发生在内容加载之前；没有注销，也没有热重载。
     * 只在主线程使用。
     */
    class DoodleComponentRegistry final
    {
    public:
        /**
         * 进程内的唯一注册表
         *
         * 函数内 static：第一次调用时构造，没有显式 Initialize / Destroy，
         * 也不存在跨翻译单元静态初始化顺序问题。
         *
         * 注册和查找都只在主线程进行。
         */
        [[nodiscard]] static DoodleComponentRegistry& GetInstance();

        /**
         * 登记一个组件类型
         *
         * 前置条件：主线程，内容加载之前。
         *
         * 会检查：
         * - name 非空；
         * - create 非空（登记了却建不出来是程序错误）；
         * - 同一 name 未登记过。
         *
         * 重复登记不会覆盖旧项，而是抛 std::logic_error —— 让两个模块抢同一个
         * 类型名的问题在启动时暴露。注册表把 name 拷贝成 map 的 key；没有注销接口。
         *
         * @throw std::invalid_argument name 为空或 create 为空
         * @throw std::logic_error 同一 name 已存在
         */
        void Register(const DoodleComponentRegistration& registration);

        /**
         * 按类型名查找注册项
         *
         * @return 找到返回注册项指针；找不到返回 nullptr
         *
         * 指针指向 map 节点，在进程生命周期内稳定；调用方应当立即使用，
         * 不要把它当成跨内容加载的长期缓存。
         *
         * 目前只给内容装配代码用；游戏层运行时不查注册表。
         */
        [[nodiscard]] const DoodleComponentRegistration* Find(std::string_view name) const;

        /**
         * 注册一个可序列化的组件类型
         *
         * 用法（注册必须在内容加载之前）：
         *
         *     DoodleComponentRegistry::RegisterComponent<DoodleCameraControl>(
         *         "GamePlay.CameraControl");
         *
         * typeName 是组件类型的稳定内容 ID；类改名不影响它，但它本身是序列化
         * 兼容性的一部分，不能随便改。必须是静态生命周期字符串（通常直接传
         * 字面量）：注册表会拷贝 key，但注册项里的 string_view 仍指向传入的字符串。
         *
         * 组件只需要实现 Deserialize；validate、create 两个类型擦除回调都由这里生成：
         * - validate 只在 Debug 下生成并调用，用一个没有宿主的临时实例跑一遍
         *   Deserialize，所以 Deserialize 不能依赖宿主 / 场景；
         * - create 走 AddComponentFromConfig，配置在 OnAwake 之前生效。
         *
         * 两个回调都是无捕获 lambda，隐式转换成对应的函数指针；不需要 std::function，
         * 也不会引入捕获对象的生命周期问题。
         *
         * 组件侧的 Serialize 仍然保留，但注册项暂时不保存序列化回调 ——
         * 等场景写入器真正落地时再接回来。
         */
        template <typename T>
        static void RegisterComponent(const std::string_view typeName)
        {
            static_assert(std::is_base_of_v<DoodleComponent, T>, "RegisterComponent<T> 的 T 必须派生自 DoodleComponent");
            static_assert(std::is_default_constructible_v<T>, "组件必须提供可访问的无参构造函数");

            DoodleComponentValidateFn validate = nullptr;

#ifndef NDEBUG
            validate = [](const DoodleComponentConfig& config)
            {
                T probe;
                probe.Deserialize(config);
            };
#endif

            GetInstance().Register({
                .name = typeName,
                .validate = validate,
                .create = [](DoodleGameObject& gameObject,
                             const DoodleComponentConfig& config)
                {
                    gameObject.AddComponentFromConfig<T>(config);
                },
            });
        }

    private:
        /**
         * 注册表随进程存在，不允许外部构造 / 销毁
         */
        DoodleComponentRegistry() = default;
        ~DoodleComponentRegistry() = default;

        DoodleComponentRegistry(const DoodleComponentRegistry&) = delete;
        DoodleComponentRegistry& operator=(const DoodleComponentRegistry&) = delete;
        DoodleComponentRegistry(DoodleComponentRegistry&&) = delete;
        DoodleComponentRegistry& operator=(DoodleComponentRegistry&&) = delete;

        /**
         * 类型名 → 注册项
         *
         * 用 map 而不是 unordered_map：注册项数量很少，节点地址稳定，
         * Find 返回的指针可以放心在本次装配里使用；std::less<> 支持
         * string_view 直接查找，不必临时构造 std::string。
         */
        std::map<std::string, DoodleComponentRegistration, std::less<>> m_registrations;
    };
}
