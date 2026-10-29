//
//  DoodleAssetLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "DoodleAssetLoadContext.h"

namespace Doodle
{
    /**
     * 类型擦除的 loader 句柄
     *
     * 资产管理器按 typeid(T) 存这一层：容器需要装不同 T 的 loader，
     * 所以必须有一个与 T 无关的公共基类。这里只放所有 loader 共有的操作
     * —— 虚析构、Destroy 与资产计数；加载入口留在 DoodleAssetLoader<T> 上，
     * 因为返回类型依赖 T，基类不可能替它声明。
     *
     * 这样 DoodleAssetManager 不用为每个 T 单独写一个槽位适配类：
     * m_loaders 直接存 IDoodleAssetLoader，DoodleAssetLoader<T> 自己
     * 继承它。注册与查找都按 typeid(T) 做键，所以 Load<T> 里下转型到
     * DoodleAssetLoader<T>* 一定成立。
     */
    class IDoodleAssetLoader
    {
    public:
        virtual ~IDoodleAssetLoader() = default;

        IDoodleAssetLoader(const IDoodleAssetLoader&) = delete;
        IDoodleAssetLoader& operator=(const IDoodleAssetLoader&) = delete;
        IDoodleAssetLoader(IDoodleAssetLoader&&) = delete;
        IDoodleAssetLoader& operator=(IDoodleAssetLoader&&) = delete;

        /**
         * 释放本 loader 持有的所有资产
         *
         * 由资产管理器在 Vulkan 设备销毁之前调用。纯 CPU 数据默认无操作。
         *
         * @note 析构器不得释放 GPU 资源：异常退出路径上资产管理器不会调用
         *       Destroy，那时可能还有在途命令；资源交给进程回收
         */
        virtual void Destroy()
        {

        }

        /**
         * 当前缓存的资产数量
         *
         * 给资产管理器做统计与泄漏检查用。纯 CPU 的 loader 也照样计数 ——
         * 它同样缓存数据，只是不持有 GPU 资源。
         *
         * 现在还没有调用者：先和 Destroy 一起放在公共面上，
         * 将来加「销毁后清单必须为空」的检查时不用再改这层接口
         */
        [[nodiscard]] virtual std::size_t GetAssetCount() const = 0;

    protected:
        /**
         * 只能由具体 loader 构造
         */
        IDoodleAssetLoader() = default;
    };

    /**
     * 单一资产类型的加载器，资产管理器内部使用
     *
     * 这是被 DoodleAssetManager 藏起来的那一半：调用方只会看到
     * assetManager.Load<T>(key)，不会看到具体是哪份 loader。
     *
     * 缓存、去重与销毁都在本类里统一实现，具体 loader 只回答一个问题：
     * 「一个 key 怎么变成一份资产」（Create）。这样新增资源类型时不用再
     * 各写一遍 map 查找 + 插入 + 销毁遍历，也不会出现某份 loader 忘了在
     * Destroy 里释放 GPU 资源 —— 那条路径只有这一处。
     *
     * 线程规则：Load 会在调用方线程执行。会创建 Vulkan 资源的 loader 只能被
     * 主线程调用；纯 CPU 的 loader 将来可以被工作线程使用。
     */
    template <typename T>
    class DoodleAssetLoader : public IDoodleAssetLoader
    {
    public:
        ~DoodleAssetLoader() override = default;

        DoodleAssetLoader(const DoodleAssetLoader&) = delete;
        DoodleAssetLoader& operator=(const DoodleAssetLoader&) = delete;
        DoodleAssetLoader(DoodleAssetLoader&&) = delete;
        DoodleAssetLoader& operator=(DoodleAssetLoader&&) = delete;

        /**
         * 加载资产
         *
         * 同一个 key 重复调用返回同一个指针：第一次调用 Create 并把结果放进
         * 缓存，之后直接命中缓存。Create 抛异常时缓存不变，不会留下一个
         * 永远加载不出来的坑位。
         *
         * @param context 本次加载可以借用的服务
         * @param key 资产键，由具体 loader 的 Create 解释（内建名、相对路径等）
         * @return 非拥有指针，寿命到 Destroy 为止
         * @throw std::runtime_error Create 失败、或 Create 返回空指针时抛出
         */
        [[nodiscard]] const T* Load(const DoodleAssetLoadContext& context, std::string_view key)
        {
            const auto it = m_assets.find(key);
            if (it != m_assets.end())
            {
                return it->second.get();
            }

            std::unique_ptr<T> asset = Create(context, key);
            if (asset == nullptr)
            {
                // 非空是 Create 的契约。让实现错误在这里当场暴露，
                // 而不是把判空的责任摊给每个调用方
                throw std::runtime_error("asset loader returned null for key: " + std::string(key));
            }

            T* pAsset = asset.get();
            m_assets.emplace(std::string(key), std::move(asset));
            return pAsset;
        }

        /**
         * 释放所有资产并清空缓存
         *
         * 先对每份资产调 DestroyAsset，再清容器：资产对象本身不负责析构期
         * 的 GPU 清理，销毁句柄只能在这里显式做。
         *
         * @note 前置条件：GPU 已空闲。由资产管理器在设备销毁之前调用
         */
        void Destroy() final
        {
            for (auto& entry : m_assets)
            {
                DestroyAsset(*entry.second);
            }

            m_assets.clear();
        }

        [[nodiscard]] std::size_t GetAssetCount() const final
        {
            return m_assets.size();
        }

    protected:
        /**
         * 只能由具体 loader 构造
         */
        DoodleAssetLoader() = default;

        /**
         * 建出一份资产
         *
         * 只负责创建，不负责缓存与去重：查重、插入、销毁都由基类处理。
         * 返回的 unique_ptr 必须非空；失败抛 std::runtime_error。
         *
         * @note 实现要保证强异常安全：中途失败不能留下已上传的 GPU 资源。
         *       资产对象随 unique_ptr 析构，而析构不做 Vulkan 清理
         */
        [[nodiscard]] virtual std::unique_ptr<T> Create(const DoodleAssetLoadContext& context, std::string_view key) = 0;

        /**
         * 释放一份资产的 GPU 资源；纯 CPU 资产不需要重写
         */
        virtual void DestroyAsset(T& asset)
        {
            (void)asset;
        }

    private:
        /**
         * key → 资产
         *
         * 用 map + 透明比较：Load 收 string_view，直接查，不必为每次调用
         * 构造一个临时的 std::string。unique_ptr 本身不移动 T，所以交出去的
         * 裸指针在后续插入时保持有效；map 的节点地址也是稳定的
         */
        std::map<std::string, std::unique_ptr<T>, std::less<>> m_assets;
    };
}
