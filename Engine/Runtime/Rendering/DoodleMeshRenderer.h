//
//  DoodleMeshRenderer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include "Core/DoodleComponent.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleMesh;

    /**
     * 网格渲染器：告诉渲染收集「这个对象用哪份网格画」
     *
     * 只保存一份网格资产的非拥有指针，不持有顶点 / 索引缓冲，也不知道
     * Vulkan。网格本身由资产管理器内部的 DoodleMeshAssetLoader 持有；
     * 销毁顺序（场景 → 资产管理器 → Vulkan）保证渲染器活着时指针一直有效。
     *
     * 空指针表示不画：对象还没有网格、或网格已经被摘掉是合法状态，
     * 收集侧跳过它，不是错误。
     *
     * 变换不在这里：位置 / 旋转 / 缩放来自宿主的 DoodleTransform，
     * 收集时现取 GetMatrix()，本类不缓存矩阵，也就不会出现「改了
     * Transform、渲染器还拿着旧矩阵」这种状态。
     */
    class DoodleMeshRenderer final : public DoodleComponent
    {
    public:
        /**
         * 设置要画的网格；传 nullptr 表示暂时不画
         *
         * 指针非拥有，寿命必须由网格库覆盖到本组件销毁为止
         */
        void SetMesh(const DoodleMesh* pMesh)
        {
            m_pMesh = pMesh;
        }

        /**
         * 当前网格，可能为空
         */
        [[nodiscard]] const DoodleMesh* GetMesh() const
        {
            return m_pMesh;
        }

    private:
        /**
         * 网格资产，非拥有；寿命由 DoodleMeshLibrary 保证
         */
        const DoodleMesh* m_pMesh = nullptr;
    };
}
