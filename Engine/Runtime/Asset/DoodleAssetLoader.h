//
//  DoodleAssetLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <string_view>

#include "DoodleAssetLoadContext.h"

namespace Doodle
{
    /**
     * 类型擦除的 loader 句柄
     *
     * 资产管理器按 typeid(T) 存这一层：容器需要装不同 T 的 loader，
     * 所以必须有一个与 T 无关的公共基类。这里只放所有 loader 共有的操作
     * —— 虚析构与 Destroy；加载入口留在 DoodleAssetLoader<T> 上，因为
     * 返回类型依赖 T，基类不可能替它声明。
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
     * assetManager.Load<T>(key)，不会看到具体是哪份 loader。每个 loader 负责：
     * - 解析该类型的文件 / 内建数据；
     * - 拥有并缓存该类型的资产，保证同一个 key 重复 Load 返回同一个指针；
     * - 在 Destroy 里释放自己持有的 GPU 资源。
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
         * @param context
         * @param key 资产键，由 loader 自己解释（内建名、相对路径等）
         * @return 非拥有指针，寿命到 Destroy 为止；同一个 key 必须返回同一指针
         * @throw std::runtime_error 读文件 / 解析 / 资源创建失败时抛出
         */
        [[nodiscard]] virtual const T* Load(const DoodleAssetLoadContext& context, std::string_view key) = 0;

    protected:
        /**
         * 只能由具体 loader 构造
         */
        DoodleAssetLoader() = default;
    };
}
