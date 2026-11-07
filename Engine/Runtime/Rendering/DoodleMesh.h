//
//  DoodleMesh.h
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#pragma once

#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Asset/DoodleAsset.h"
#include "DoodleSubmesh.h"
#include "DoodleVertex.h"
#include "Vulkan/DoodleGraphicsBuffer.h"

namespace Doodle
{
    /**
     * 一份网格资产：顶点缓冲 + 索引缓冲 + 子网格区间 + 顶点格式标签
     *
     * 放在 Rendering：它属于「画什么」里的几何内容；Vulkan 侧的
     * DoodleGraphicsBuffer 才是「怎么存」的显存机制。本类按值持有那份
     * 机制，对外暴露的仍是网格语义。
     *
     * 几何数据住在设备本地显存里，本类只负责这组资源的生死。它不知道
     * GameObject、Transform、材质或绘制命令 —— 谁摆在哪里、画成什么颜色，
     * 是场景与渲染收集的事。
     *
     * 子网格不另建缓冲：整份网格仍只有一对顶点/索引缓冲，DoodleSubmesh 只是
     * 索引缓冲里的连续区间加上材质槽。绘制端按区间分段调用 vkCmdDrawIndexed，
     * 换材质不必拆资源，也不必每次把整份网格重新提交。区间由导入器按材质切好，
     * Initialize 会校验它们有序、连续、覆盖整个索引缓冲。
     *
     * 网格不保存材质数据：submesh.materialIndex 只是槽号，真正的 DoodleMaterial
     * 由 DoodleMeshRenderer 按槽引用共享材质资产。同一份网格可以被多个对象共用、
     * 各自配不同材质资产，资产缓存不会和外观绑死。
     *
     * 顶点格式跟着网格走：Initialize 从顶点类型（TVertex::kVertexFormat）取得
     * 格式并保存在这里，绘制端按它选管线（管线缓存的键里含这一维）。本类不
     * 理解格式的内存布局 —— 那是 DoodleVertex.h 的事，这里只记一个枚举值。
     *
     * 资产路径与「运行时动态创建」的约定在基类 DoodleAsset 里：文件资产的
     * 路径就是 loader 的缓存键，空串表示动态创建、不能序列化。本类只关心
     * 网格自己的资源、顶点格式与子网格表。
     *
     * 不可复制、不可移动（基类 DoodleAsset 已禁止）：两个 DoodleMesh 对象会
     * 各自销毁同一对 VkBuffer。使用方拿到的是 const 指针（非拥有）：文件资产的
     * 寿命由 loader 保证，运行时动态创建的实例由创建方保证；两者都要在
     * GPU 空闲时显式 Destroy。loader 内部则用 unique_ptr 持有，不存在需要
     * 搬运对象本身的场景
     *
     * 索引类型固定为 uint32_t：FBX 拍平后顶点数很容易越过 65535，
     * 与 DoodleCommandManager::Record 里的 VK_INDEX_TYPE_UINT32 是同一个决定。
     * 不按模型大小分两套索引类型
     */
    class DoodleMesh : public DoodleAsset
    {
    public:
        /**
         * 运行时动态创建：没有资产路径，不能被序列化
         */
        DoodleMesh() = default;

        /**
         * 文件资产：路径交给 DoodleAsset 保存并校验非空
         *
         * @param assetPath 资产的加载路径（当前是相对文件路径）
         */
        explicit DoodleMesh(std::string_view assetPath);

        /**
         * 把两份数据与子网格区间送上设备
         *
         * 顶点类型是模板参数，但只有这一层是泛型的：所有校验与上传都在
         * InitializeBuffers 里做一次，模板只负责把 vector 拆成
         * 「首地址 + 字节数」，再把类型自带的格式标签递过去。
         *
         * 为什么不是每种顶点结构一个重载：重载会把同一段逻辑抄 N 遍，
         * 而类型之间真正不同的只有 sizeof 和 kVertexFormat 两件事。
         * 为什么 DoodleMesh 本身不是模板类：资产管理器、loader、网格池
         * 与序列化都要一个具体类型，泛型只能停在入口这一层。
         *
         * TVertex 必须声明 kVertexFormat（见 DoodleVertex.h）：少了它编译不过，
         * 随便传一个 vector<uint32_t> 也编译不过
         *
         * @param device 已初始化的 Vulkan 设备，且命令管理层的传输池已经建好
         * @param vertices 顶点数据，按 TVertex 的布局排列
         * @param indices 索引数据，每个值都必须小于 vertices.size()
         * @param submeshes 按材质切好的索引区间；必须有序、首尾相接、覆盖整个
         *        indices；materialIndex 从 0 开始连续分配
         *
         * @throw std::runtime_error 子网格区间不满足上面那组不变量。
         *        校验发生在任何 GPU 资源创建之前 —— 本类析构不兜底，
         *        不能在上传一半时抛出而留下无主的缓冲
         *
         * 收 const vector& 而不是 span：当前唯一调用方（DoodleMeshLoader）传的
         * 就是导入结果的 vector，视图类型没有实际收益；出现第二种容器 /
         * 子区间时再泛化
         *
         * @note 内部会提交一次拷贝并等待完成，只能在初始化期调用，
         *       不得与在途的帧并发 —— 见 DoodleGraphicsBuffer::CopyTo
         */
        template <typename TVertex>
        void Initialize(const DoodleVulkanDevice& device,
                        const std::vector<TVertex>& vertices,
                        const std::vector<uint32_t>& indices,
                        const std::vector<DoodleSubmesh>& submeshes)
        {
            InitializeBuffers(device,
                              vertices.data(),
                              vertices.size() * sizeof(TVertex),
                              TVertex::kVertexFormat,
                              indices,
                              submeshes);
        }

        /**
         * 销毁两份缓冲
         *
         * @note 前置条件：GPU 已空闲。文件资产由 loader 的 DestroyAsset 统一调用；
         *       运行时动态创建的实例由创建方调用
         */
        void Destroy();

        /**
         * 顶点缓冲句柄
         */
        [[nodiscard]] VkBuffer GetVertexBuffer() const
        {
            return m_vertexBuffer.GetBuffer();
        }

        /**
         * 索引缓冲句柄
         */
        [[nodiscard]] VkBuffer GetIndexBuffer() const
        {
            return m_indexBuffer.GetBuffer();
        }

        /**
         * 顶点格式：这份网格的顶点缓冲按哪种布局组织
         *
         * 绘制端按它选管线（管线缓存的键里含这一维），所以格式跟着网格走，
         * 不只跟着管线走。Initialize 时由 TVertex::kVertexFormat 写入；
         * 在此之前读到的是枚举的 0 值，和缓冲为空一样属于「未初始化」
         */
        [[nodiscard]] EVertexFormat GetVertexFormat() const
        {
            return m_vertexFormat;
        }

        /**
         * 取第 submeshIndex 段子网格
         *
         * 返回网格持有的区间引用，不拷贝，寿命与网格相同。
         *
         * 下标必须落在 [0, GetSubmeshes().size())，本函数不做边界检查：
         * 调用方的下标来自收集侧对同一张表的遍历（DoodleDrawItem::submeshIndex），
         * 表在 Initialize 之后不可变，因此不会失效。给出越界下标是调用方的错误，
         * 不是可恢复状态。
         *
         * 与 GetSubmeshes() 的分工：绘制端一次只要一段，用这个避免把整张表
         * 暴露在调用点；需要遍历全部段（收集、材质槽统计）时再用另一个。
         */
        [[nodiscard]] const DoodleSubmesh& GetSubmesh(uint32_t submeshIndex) const
        {
            return m_submeshes[submeshIndex];
        }

        /**
         * 按材质切好的索引区间；绘制端逐段发 vkCmdDrawIndexed
         *
         * 区间有序、连续、覆盖整个索引缓冲，由 Initialize 校验
         */
        [[nodiscard]] const std::vector<DoodleSubmesh>& GetSubmeshes() const
        {
            return m_submeshes;
        }

        /**
         * 材质槽数量：子网格里最大 materialIndex + 1
         *
         * 导入器从 0 开始连续分配槽号，所以这个值就是 renderer 需要创建的
         * DoodleMaterial 个数。每次现算（子网格只有几段），不另存一份可能与
         * 子网格表漂移的状态
         */
        [[nodiscard]] uint32_t GetMaterialSlotCount() const
        {
            uint32_t slotCount = 0;
            for (const DoodleSubmesh& submesh : m_submeshes)
            {
                const uint32_t slot = submesh.materialIndex + 1;
                if (slot > slotCount)
                {
                    slotCount = slot;
                }
            }
            return slotCount;
        }

    private:
        /**
         * 模板入口的唯一实现：校验子网格，再把两份数据送上设备
         *
         * 收 void* + 字节数而不是某个顶点类型的 vector：这里是类型擦除的边界，
         * 也是「泛型只占一层」的原因 —— 校验、描述、上传都只有这一份
         *
         * @param vertexData 顶点数据首地址，按 vertexFormat 的布局排列
         * @param vertexDataSize 顶点数据的字节数
         */
        void InitializeBuffers(const DoodleVulkanDevice& device,
                               const void* vertexData,
                               const VkDeviceSize vertexDataSize,
                               const EVertexFormat vertexFormat,
                               const std::vector<uint32_t>& indices,
                               const std::vector<DoodleSubmesh>& submeshes);

    private:
        /**
         * 顶点格式标签，Initialize 时从 TVertex::kVertexFormat 写入
         *
         * Destroy 不改它：那时缓冲已经空了，格式和空句柄一样不指向任何数据
         */
        EVertexFormat m_vertexFormat = EVertexFormat::PosNormalUv;

        /**
         * 顶点缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_vertexBuffer;

        /**
         * 索引缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_indexBuffer;

        /**
         * 子网格区间，CPU 侧数据；索引下标指向 m_indexBuffer，
         * materialIndex 由 renderer 解释
         */
        std::vector<DoodleSubmesh> m_submeshes;
    };
}
