//
//  DoodleAssetManager.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "DoodleAssetLoader.h"

namespace Doodle
{
    class DoodleApplication;
    class DoodleFileSystem;
    class DoodleVulkanDevice;

    /**
     * 资产管理器初始化时由应用提供的服务
     *
     * 应用是组合根：它知道文件系统与 Vulkan 各层，但不需要知道任何具体 loader。
     * 这里只把 loader 构造需要的服务传进来；将来新增资源类型需要更多服务时，
     * 往这个结构体加字段即可，Initialize 的调用点只有一处。
     */
    struct DoodleAssetManagerInitContext
    {
        const DoodleFileSystem& fileSystem;
        DoodleVulkanDevice& device;
    };

    /**
     * 资产管理器：所有资源的统一入口
     *
     * 对外只有 Load<T>(key)。T 是资产类型键：
     *   Load<DoodleMesh>("builtin:cube")
     *   Load<DoodleSceneData>("Scenes/main.json")
     * 具体用哪个 loader、走文件还是内建数据，调用方不需要知道。
     *
     * 类型注册表与内建 loader 都藏在本类内部：Initialize 时一次性注册，
     * 加新资源类型只改本类 .cpp 的 RegisterBuiltinLoaders 和新增 loader，
     * 调用点与 DoodleApplication 都不动。
     *
     * 资产的缓存与所有权在 DoodleAssetLoader<T> 基类里（key → unique_ptr<T>），
     * 本类只负责分发与生命周期。返回的 const T* 在 Destroy 之前有效。
     *
     * 运行时只有一份，GamePlay 层通过 GetInstance() 取用；所有权和生命周期
     * 仍在 DoodleApplication 手里，本类不自己创建实例。
     *
     * 生命周期：由 DoodleApplication 持有，必须比 DoodleSceneManager 晚销毁、
     * 比 DoodleVulkanManager 早销毁 —— 场景里的 MeshRenderer 引用着网格，
     * 而网格缓冲必须在设备之前释放。应用在 WaitIdle 之后显式调用 Destroy；
     * 析构不做 GPU 清理，避免异常退出时在途命令仍引用着缓冲。
     */
    class DoodleAssetManager
    {
        friend class DoodleApplication;

    private:
        /**
         * 只能由 DoodleApplication 创建：资产管理器在运行时只有一份，实例由应用
         * 独占持有。构造即登记为当前实例，析构时清空，和 DoodleSceneManager
         * 的登记方式一致。
         */
        DoodleAssetManager();

    public:
        ~DoodleAssetManager();

        DoodleAssetManager(const DoodleAssetManager&) = delete;
        DoodleAssetManager& operator=(const DoodleAssetManager&) = delete;
        DoodleAssetManager(DoodleAssetManager&&) = delete;
        DoodleAssetManager& operator=(DoodleAssetManager&&) = delete;

        /**
         * 当前资产管理器实例
         *
         * 存在的理由：GamePlay 代码在组件回调这类地方拿不到应用对象，而资产
         * 系统本来就是进程里唯一的那份。场景加载（DoodleScene::
         * InitializeFromData）也从这里取 —— 场景本身不保存资产管理器的引用；
         * 其余运行时路径仍以显式传引用为准（见 DoodleApplication::
         * InitializeRuntime）。
         *
         * 有效期就是资产管理器对象的存活期：构造时登记、析构时清空，成对维护。
         * 实例仍由 DoodleApplication 独占持有 —— 这里只是访问出口，不是
         * 「第一次调用时自己创建一个」的写法；那样生命周期就不再跟着应用与
         * Vulkan 的拆解顺序走了。
         *
         * 在 Initialize 之前取到实例是合法的，但 Load 会抛 std::logic_error。
         * 进程里没有资产管理器时（构造之前、析构之后）取用是未定义行为。
         * 返回引用而不是指针：资产管理器随应用对象一起存在，应用活着就意味着
         * 它已经登记，那个空状态没有调用者够得着。
         */
        [[nodiscard]] static DoodleAssetManager& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 初始化并注册内建 loader
         *
         * 前置条件：Vulkan 设备与命令缓冲层已经初始化（网格 loader 要上传内建数据），
         * 文件系统对象已经存在。重复调用属于错误用法，抛 std::logic_error。
         */
        void Initialize(const DoodleAssetManagerInitContext& context);

        /**
         * 销毁所有 loader 与它们持有的资产
         *
         * 前置条件：GPU 已空闲。由应用在 Vulkan 设备销毁之前调用，重复调用幂等。
         */
        void Destroy();

        /**
         * 加载指定类型的资产
         *
         * @param key 资产键，由该类型的 loader 解释
         * @return 非拥有指针，寿命到 Destroy 为止；同一个 key 重复调用返回同一指针
         * @throw std::logic_error 系统未初始化、或该类型没有注册 loader
         * @throw std::runtime_error 文件读取 / 解析 / 资源创建失败
         */
        template <typename T>
        [[nodiscard]] const T* Load(std::string_view key)
        {
            if (m_pFileSystem == nullptr)
            {
                throw std::logic_error("DoodleAssetManager: Load called before Initialize");
            }

            const auto it = m_loaders.find(std::type_index(typeid(T)));
            if (it == m_loaders.end())
            {
                throw std::logic_error(std::string("DoodleAssetManager: no loader registered for ") + typeid(T).name());
            }

            // 注册与查找都按 typeid(T) 做键，所以这个下转型一定成立：
            // 该键下只可能是 DoodleAssetLoader<T> 的派生对象。
            // loader 在 Initialize 时已经持有固定的 fileSystem / assetManager，
            // 这里只按 key 分发，不再每次加载临时组 context
            auto* pLoader = static_cast<DoodleAssetLoader<T>*>(it->second.get());
            return pLoader->Load(key);
        }

    private:
        /**
         * 注册 loader；同一类型只允许一次
         */
        template <typename T>
        void RegisterLoader(std::unique_ptr<DoodleAssetLoader<T>> pLoader)
        {
            if (pLoader == nullptr)
            {
                throw std::logic_error("DoodleAssetManager: loader is null");
            }

            const std::type_index type(typeid(T));
            if (m_loaders.contains(type))
            {
                throw std::logic_error(std::string("DoodleAssetManager: loader already registered for ") + typeid(T).name());
            }

            // 公共基类只保留与 T 无关的析构与 Destroy；DoodleAssetLoader<T>
            // 自己继承它，所以这里直接交给容器，不再需要额外的槽位包装
            m_loaders.emplace(type, std::unique_ptr<IDoodleAssetLoader>(std::move(pLoader)));
        }

    private:
        /**
         * 当前实例，构造时登记、析构时清空
         */
        static DoodleAssetManager* s_pInstance;

        /**
         * 文件来源，非拥有；寿命由 DoodleApplication 保证
         *
         * 它同时充当「系统已初始化」的标志：nullptr 表示还没 Initialize。
         */
        const DoodleFileSystem* m_pFileSystem = nullptr;

        /**
         * 类型 → loader。系统独占 loader；loader 独占它加载出来的资产
         *
         * 元素类型是类型擦除后的 IDoodleAssetLoader；DoodleAssetLoader<T>
         * 是它的 typed 派生，注册与查找用同一个 typeid(T) 配对
         */
        std::unordered_map<std::type_index, std::unique_ptr<IDoodleAssetLoader>> m_loaders;
    };
}
