//
//  DoodleMeshRenderer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include "Utility/DoodleCommon.h"

#include "Core/DoodleComponent.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleMesh;
    class DoodleMaterial;

    /**
     * 网格渲染器：告诉渲染收集「这个对象用哪份网格、配哪些材质」
     *
     * 只保存网格资产与材质资产的非拥有指针，不持有顶点 / 索引缓冲，也不知道
     * Vulkan。网格由 DoodleMeshLoader 持有，材质由 DoodleMaterialLoader 持有；
     * 销毁顺序（场景 → 资产管理器 → Vulkan）保证渲染器活着时两者一直有效。
     *
     * 材质归 renderer 的槽表而不是网格：网格是共享资产，同一份网格可以被多个
     * 对象使用；每个槽引用一个共享材质资产。同一份网格的不同 renderer 可以给
     * 同一个槽配不同材质资产。槽可以为空：空表示这个子网格没有材质，收集侧
     * 跳过它、不生成绘制项 —— 渲染器不提供默认材质兜底。材质带着自己的着色器，
     * 管线由绘制侧的 pass 按着色器解析。
     *
     * 空网格指针表示不画：对象还没有网格、或网格已经被摘掉是合法状态，
     * 收集侧跳过它，不是错误。换网格时槽表长度改为新网格的槽数：同槽号上
     * 已有的材质保留，新出现的槽留空，缩掉的槽丢弃。
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
         * 材质表按槽顺序写成资产路径数组，空指针槽写成空串 —— 读回时仍然
         * 表示「该槽没有材质」，不画对应的子网格。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从序列化配置读取 mesh 路径与材质表，并向资产管理器取网格
         *
         * 路径只用于本次 Load，随后由 DoodleMesh 自己记录，供以后 Serialize。
         * SetMesh 先把槽表调整到网格槽数：同槽号上已有的材质保留、新槽留空；
         * materials 里给出的路径再逐槽装入，空串表示该槽没有材质，保持为空。
         * 条目数超过网格槽数时抛异常。这个方法只做字段/资产解析，不依赖宿主，
         * 所以 Debug 校验探针也能调用。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        /**
         * 按材质槽排列的材质表；submesh.materialIndex 指向这里
         *
         * 元素是非拥有指针，可能为空：空表示该槽没有材质，对应子网格不画。
         * 长度恒等于当前网格的材质槽数；没有网格时为空
         */
        [[nodiscard]] const std::vector<const DoodleMaterial*>& GetMaterials() const
        {
            return m_pMaterials;
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
         * 槽表长度改为新网格的材质槽数：同槽号上已有的材质保留，新出现的槽留空
         * （空指针 = 该子网格不画），缩掉的槽丢弃；传 nullptr 时槽表清空。
         * 本方法不加载任何资产，也不会失败 —— 材质只经 Deserialize 装入，
         * 所以不存在「加载失败留下新网格配旧材质」的中间状态。
         * 本类不记住旧网格的路径；Serialize 写的是当前网格自己的 assetPath。
         */
        void SetMesh(const DoodleMesh* pMesh);

    private:
        /**
         * 按槽排列的材质指针，一个子网格材质槽一项
         */
        std::vector<const DoodleMaterial*> m_pMaterials;

        /**
         * 网格资产，非拥有；寿命由加载它的 loader 保证（资产管理器 Destroy 之前）
         */
        const DoodleMesh* m_pMesh = nullptr;
    };
}
