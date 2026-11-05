//
//  DoodleMeshRenderer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <vector>

#include "Core/DoodleComponent.h"
#include "DoodleMaterial.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleMesh;

    /**
     * 网格渲染器：告诉渲染收集「这个对象用哪份网格、配哪些材质」
     *
     * 只保存网格资产与材质资产的非拥有指针，不持有顶点 / 索引缓冲，也不知道
     * Vulkan。网格由 DoodleMeshLoader 持有，材质由 DoodleMaterialLoader 持有；
     * 销毁顺序（场景 → 资产管理器 → Vulkan）保证渲染器活着时两者一直有效。
     *
     * 材质归 renderer 的槽表而不是网格：网格是共享资产，同一份网格可以被多个
     * 对象使用；每个槽引用一个共享材质资产。同一份网格的不同 renderer 可以给
     * 同一个槽配不同材质资产。没有配置材质资产的槽指向本 renderer 自带的默认材质。
     *
     * 空网格指针表示不画：对象还没有网格、或网格已经被摘掉是合法状态，
     * 收集侧跳过它，不是错误。换网格时材质槽表按新网格的槽数重建为默认材质。
     *
     * 变换不在这里：位置 / 旋转 / 缩放来自宿主的 DoodleTransform，
     * 收集时现取 GetLocalToWorldMatrix()，本类不缓存矩阵，也就不会出现「改了
     * Transform、渲染器还拿着旧矩阵」这种状态。
     */
    class DoodleMeshRenderer final : public DoodleComponent
    {
    public:
        /**
         * 把当前网格的 assetPath 与材质表写回序列化配置
         *
         * 路径从 DoodleMesh 取，renderer 不再另存 key。运行时通过 SetMesh
         * 换过网格时，写回的是新网格的路径。当前网格没有 assetPath（运行时
         * 动态创建）或没有网格时写空串 —— 这种状态不能被反序列化回来。
         * 材质表按槽顺序写成资产路径数组，空串表示该槽使用默认材质。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从序列化配置读取 mesh 路径与材质表，并向资产管理器取网格
         *
         * 路径只用于本次 Load，随后由 DoodleMesh 自己记录，供以后 Serialize。
         * 网格与默认材质表通过 SetMesh 安装，materials 里给出的路径再覆盖默认值：
         * 缺省或比网格槽数少时，缺的槽保持默认；多出来的条目抛异常。
         * 路径非空时向资产管理器取共享材质资产；同一路径在各对象之间共享，
         * 空串表示该槽使用默认材质。这个方法只做字段/资产解析，不依赖宿主，
         * 所以 Debug 校验探针也能调用。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        /**
         * 按材质槽排列的材质表；submesh.materialIndex 指向这里
         *
         * 元素是非拥有指针：共享材质由资产管理器持有，默认材质由本组件持有。
         * 长度等于当前网格的材质槽数；没有网格时为空
         */
        [[nodiscard]] const std::vector<const DoodleMaterial*>& GetMaterials() const
        {
            return m_materials;
        }

        /**
         * 当前网格，可能为空
         */
        [[nodiscard]] const DoodleMesh* GetMesh() const
        {
            return m_pMesh;
        }

        /**
         * 设置要画的网格；传 nullptr 表示暂时不画
         *
         * 指针非拥有，寿命必须由网格库覆盖到本组件销毁为止。
         * 材质表按新网格的材质槽数重建为默认材质（指针指向本组件的默认材质）——
         * 换网格后槽位含义变了，保留旧材质只会串到别的子网格上。需要自定义材质时，
         * 换完网格再设置。反序列化也走这个入口：先装网格与默认材质，再由
         * Deserialize 覆盖配置。本类不记住旧网格的路径；Serialize 写的是当前
         * 网格自己的 assetPath。
         */
        void SetMesh(const DoodleMesh* pMesh);

    private:
        /**
         * 默认材质：没有配置材质资产的槽指向它
         *
         * 按值持有是为了给槽表一个稳定地址；共享材质资产仍由资产管理器持有。
         * 声明在 m_materials 之前：析构时槽表先销毁，它再销毁
         */
        DoodleMaterial m_defaultMaterial;

        /**
         * 按槽排列的材质指针，一个子网格材质槽一项
         */
        std::vector<const DoodleMaterial*> m_materials;

        /**
         * 网格资产，非拥有；寿命由加载它的 loader 保证（资产管理器 Destroy 之前）
         */
        const DoodleMesh* m_pMesh = nullptr;
    };
}
