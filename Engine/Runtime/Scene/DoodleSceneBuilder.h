//
//  DoodleSceneBuilder.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

namespace Doodle
{
    class DoodleAssetManager;
    class DoodleScene;
    class DoodleSceneManager;
    struct DoodleSceneData;

    /**
     * 场景实例化器：把 DoodleSceneData 变成运行中的 GameObject
     *
     * 只做「数据 → 世界」，不做文件读取与 JSON 解析；那些属于
     * DoodleSceneLoader。场景加载也不负责加载资产 —— 它通过资产管理器按名字
     * 取已经就绪的网格，取不到就报错。
     *
     * 先解析所有网格、再创建场景与对象：网格名写错时一个对象也不会建出来，
     * 不会留下半个场景。只能在主线程调用（会创建 GameObject 与组件）。
     */
    class DoodleSceneBuilder
    {
    public:
        DoodleSceneBuilder() = delete;

        /**
         * 按数据建出场景，返回它的引用
         *
         * @param sceneManager 场景的拥有者
         * @param assetManager 解析网格等资产用
         * @param sceneData 已解析的数据
         * @throw std::runtime_error 引用的资产不存在时抛出，此时不会创建场景
         */
        static DoodleScene& Build(DoodleSceneManager& sceneManager,
                                  DoodleAssetManager& assetManager,
                                  const DoodleSceneData& sceneData);
    };
}
