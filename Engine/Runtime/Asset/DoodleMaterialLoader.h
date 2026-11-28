//
//  DoodleMaterialLoader.h
//  Doodle
//
//  由 DoodleMaterial 资产化新增
//

#pragma once

#include "Utility/DoodleCommon.h"

#include "DoodleAssetLoader.h"
#include "Rendering/DoodleMaterial.h"

namespace Doodle
{
    class DoodleFileSystem;

    /**
     * 材质加载器：Materials/ 下的 JSON 文件 → DoodleMaterial
     *
     * 文件结构是一个 JSON object，三个字段都可省略：
     *   {
     *     "baseColor": [r, g, b, a],
     *     "textures": { "albedoTexture": "Art/SceneRes/Building/House1/House1_d.tga" },
     *     "shader": "Shaders/lit.shader.json"
     *   }
     *
     * textures 的键是贴图槽名 —— 着色器 set 1 里贴图的 GLSL 变量名，着色器声明了
     * 哪几张就能配哪几张；值是相对 Resources 根的资产路径，与场景文件同一口径。
     * 槽名不属于这个着色器时加载失败（不分 Debug / Release）：拼错的名字会让那张
     * 贴图悄悄变成白图。值为空串、或整个槽省略，表示不配，绘制侧绑定默认 1×1 白图。
     * baseColor 按 sRGB 书写。shader 省略时用 lit。加载器只产出内容（基色 / 贴图 /
     * 着色器），不解析管线 —— 管线由录制侧的各个 pass 按自己的描述现取。
     *
     * 缓存与去重由 DoodleAssetLoader 基类按 key 负责；材质不持有 GPU 资源，
     * 因此不需要重写 DestroyAsset。
     *
     * 线程约定：加载会递归加载纹理 / 着色器（创建 Vulkan 资源），只能在主线程、
     * 初始化期调用。
     */
    class DoodleMaterialLoader final : public DoodleAssetLoader<DoodleMaterial>
    {
    public:
        explicit DoodleMaterialLoader(const DoodleFileSystem& fileSystem);

        DoodleMaterialLoader(const DoodleMaterialLoader&) = delete;
        DoodleMaterialLoader& operator=(const DoodleMaterialLoader&) = delete;

    protected:
        /**
         * 读取并解析一份材质文件，再递归加载它引用的着色器与贴图
         *
         * @throws std::runtime_error 文件读取 / JSON 解析失败、字段类型不对、
         *         贴图槽名不属于着色器，或引用的贴图 / 着色器加载失败
         */
        [[nodiscard]] std::unique_ptr<DoodleMaterial> Create(std::string_view key) override;
    };
}
