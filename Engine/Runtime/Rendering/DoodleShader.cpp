//
//  DoodleShader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#include "DoodleShader.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../Vulkan/DoodlePipelineWarmupQueue.h"
#include "Vulkan/DoodleDescriptorManager.h"
#include "Vulkan/DoodleVulkanDevice.h"
#include "Vulkan/DoodleVulkanPipelineObject.h"
#include "Rendering/DoodleShaderData.h"
#include "spirv_reflect/spirv_reflect.h"

namespace Doodle
{
    DoodleShader::DoodleShader(const std::string_view assetPath)
        : DoodleAsset(assetPath)
    {
    }

    void DoodleShader::Initialize(const DoodleVulkanDevice& device,
                                  const std::vector<std::byte>& vertexBytecode,
                                  const std::vector<std::byte>& fragmentBytecode,
                                  const DoodleRenderState& renderState,
                                  const EVertexFormat vertexFormat,
                                  const EInstanceFormat instanceFormat)
    {
        if (vertexBytecode.empty())
        {
            throw std::runtime_error("vertex bytecode is empty");
        }
        if (fragmentBytecode.empty())
        {
            throw std::runtime_error("fragment bytecode is empty");
        }

        m_pDevice = &device;

        //先反射与核对、再动设备：不合法的着色器不留下任何句柄。
        //失败时 Destroy 只会在句柄为空时跳过，等于把对象恢复成未初始化。
        //错误消息不再加资产路径：调用方（加载器）会补上，这里加一遍会重复
        try
        {
            std::vector<ReflectedBinding> bindings;
            std::vector<VkPushConstantRange> pushConstantRanges;
            ReflectStage(vertexBytecode, VK_SHADER_STAGE_VERTEX_BIT, bindings, pushConstantRanges);
            ReflectStage(fragmentBytecode, VK_SHADER_STAGE_FRAGMENT_BIT, bindings, pushConstantRanges);

            const std::vector<VkDescriptorSetLayoutBinding> materialBindings =
                ValidateInterface(bindings, pushConstantRanges);
            CreateMaterialSetLayout(materialBindings);
        }
        catch (...)
        {
            Destroy();
            throw;
        }

        //字节码各拷一份留在描述里：材质向管线表要管线时，表按内容做键并把这份
        //描述连同字节码存进条目，格式变化原地重建时用它
        m_forwardPassDescription.vertexBytecode = vertexBytecode;
        m_forwardPassDescription.fragmentBytecode = fragmentBytecode;
        m_forwardPassDescription.renderState = renderState;
        m_forwardPassDescription.vertexFormat = vertexFormat;
        m_forwardPassDescription.instanceFormat = instanceFormat;

        //一切状态都已就绪，此刻起可以拿这份描述向任何 pass 的管线表要管线。
        //投递给渲染侧，在下一帧取帧之前统一建出来，不落进命令录制。
        //
        //放在 Initialize 而不是加载器里：这是着色器「变得可用」的唯一状态转换点，
        //运行期动态创建的着色器也走同一条路，不依赖某个调用者记得通知
        DoodlePipelineWarmupQueue::GetInstance().Push(*this);
    }

    void DoodleShader::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //set 布局可能与管线表里的管线布局存在「出生关系」，但那条依赖只在创建
        //管线布局时读过一次 —— 本资产先于管线表销毁是合法的（见类注释）
        if (m_forwardPassDescription.materialDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(),
                                         m_forwardPassDescription.materialDescriptorSetLayout,
                                         nullptr);
            m_forwardPassDescription.materialDescriptorSetLayout = VK_NULL_HANDLE;
        }

        m_forwardPassDescription.vertexBytecode.clear();
        m_forwardPassDescription.fragmentBytecode.clear();
        m_pDevice = nullptr;
    }

    void DoodleShader::ReflectStage(const std::vector<std::byte>& bytecode,
                                    VkShaderStageFlagBits expectedStage,
                                    std::vector<ReflectedBinding>& bindings,
                                    std::vector<VkPushConstantRange>& pushConstantRanges)
    {
        const char* const stageName = GetStageName(expectedStage);

        //反射结果在 module 析构时释放（C++ 封装的 RAII）；本函数用完即弃，
        //后面只保留它读出来的值与偏移
        const spv_reflect::ShaderModule module(bytecode.size(), bytecode.data());
        if (module.GetResult() != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to reflect ") + stageName + " shader (SPIRV-Reflect result " +
                                     std::to_string(static_cast<int>(module.GetResult())) + ")");
        }

        const SpvReflectShaderStageFlagBits expectedReflectStage =
            expectedStage == VK_SHADER_STAGE_VERTEX_BIT
                ? SPV_REFLECT_SHADER_STAGE_VERTEX_BIT
                : SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT;
        if (module.GetShaderStage() != expectedReflectStage)
        {
            throw std::runtime_error(std::string("the ") + stageName +
                                     " bytecode is not a " + stageName + " shader");
        }

        //管线固定按 pName = "main" 建阶段；入口点不叫 main 的字节码在这里就拦下，
        //而不是等 vkCreateGraphicsPipelines 报一句看不出原因的错误
        const char* pEntryPoint = nullptr;
        for (uint32_t i = 0; i < module.GetEntryPointCount(); ++i)
        {
            const char* const pName = module.GetEntryPointName(i);
            if (pName != nullptr && std::string_view(pName) == "main")
            {
                pEntryPoint = pName;
            }
        }
        if (pEntryPoint == nullptr)
        {
            throw std::runtime_error(std::string("the ") + stageName +
                                     " shader has no entry point named 'main'");
        }

        //描述符绑定：按入口点取。同一份资源可能在两个阶段都声明（set 0 的 UBO
        //就是），所以合并到同一张表里，阶段取并集
        uint32_t bindingCount = 0;
        if (module.EnumerateEntryPointDescriptorBindings(pEntryPoint, &bindingCount, nullptr) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to enumerate ") + stageName + " descriptor bindings");
        }
        std::vector<SpvReflectDescriptorBinding*> stageBindings(bindingCount);
        if (bindingCount > 0 &&
            module.EnumerateEntryPointDescriptorBindings(pEntryPoint, &bindingCount, stageBindings.data()) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to enumerate ") + stageName + " descriptor bindings");
        }

        for (const SpvReflectDescriptorBinding* pStageBinding : stageBindings)
        {
            ReflectedBinding reflected;
            reflected.set = pStageBinding->set;
            reflected.binding = pStageBinding->binding;
            reflected.descriptorType = static_cast<VkDescriptorType>(pStageBinding->descriptor_type);
            reflected.descriptorCount = pStageBinding->count;
            reflected.stageFlags = expectedStage;

            //uniform / storage 块：记下着色器声明的字节数，下面核对它没有超出
            //C++ 侧那块缓冲
            if (reflected.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                reflected.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            {
                reflected.blockSize = pStageBinding->block.size;
            }

            const auto sameBinding = std::find_if(bindings.begin(), bindings.end(),
                                                  [&reflected](const ReflectedBinding& existing)
                                                  {
                                                      return existing.set == reflected.set &&
                                                             existing.binding == reflected.binding;
                                                  });

            if (sameBinding == bindings.end())
            {
                bindings.push_back(reflected);
                continue;
            }

            //两个阶段声明同一个绑定：类型与数量必须一致，阶段取并集。
            //块大小不一致说明两边的声明已经漂移，这是内容错误，不静默取其一
            if (sameBinding->descriptorType != reflected.descriptorType ||
                sameBinding->descriptorCount != reflected.descriptorCount ||
                sameBinding->blockSize != reflected.blockSize)
            {
                throw std::runtime_error(
                    "set " + std::to_string(reflected.set) + " binding " + std::to_string(reflected.binding) +
                    " is declared differently in the vertex and fragment shaders");
            }
            sameBinding->stageFlags |= expectedStage;
        }

        //push constant：记下这个阶段用到的那一段（偏移与大小）。可能没有，也可能
        //只有部分成员；核对交给 ValidateInterface
        uint32_t blockCount = 0;
        if (module.EnumerateEntryPointPushConstantBlocks(pEntryPoint, &blockCount, nullptr) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to enumerate ") + stageName + " push constant blocks");
        }
        std::vector<SpvReflectBlockVariable*> pushConstantBlocks(blockCount);
        if (blockCount > 0 &&
            module.EnumerateEntryPointPushConstantBlocks(pEntryPoint, &blockCount, pushConstantBlocks.data()) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to enumerate ") + stageName + " push constant blocks");
        }

        for (const SpvReflectBlockVariable* pBlock : pushConstantBlocks)
        {
            pushConstantRanges.push_back(
            {
                .stageFlags = expectedStage,
                .offset = pBlock->offset,
                .size = pBlock->size
            });
        }
    }

    std::vector<VkDescriptorSetLayoutBinding> DoodleShader::ValidateInterface(
        const std::vector<ReflectedBinding>& bindings,
        const std::vector<VkPushConstantRange>& pushConstantRanges)
    {
        std::vector<VkDescriptorSetLayoutBinding> materialBindings;
        const ReflectedBinding* pFrameBinding = nullptr;
        std::size_t frameBindingCount = 0;

        for (const ReflectedBinding& reflected : bindings)
        {
            switch (reflected.set)
            {
            case 0:
                ++frameBindingCount;
                pFrameBinding = &reflected;
                break;

            case 1:
                materialBindings.push_back(
                {
                    .binding = reflected.binding,
                    .descriptorType = reflected.descriptorType,
                    .descriptorCount = reflected.descriptorCount,
                    .stageFlags = reflected.stageFlags,
                    .pImmutableSamplers = nullptr
                });
                break;

            default:
                //所有管线共用 set 0；set 1 是材质资源。第三个集合没有宿主，
                //声明了也没有人能给它分配集合
                throw std::runtime_error("set " + std::to_string(reflected.set) +
                                         " is not supported (only set 0 and set 1 are)");
            }
        }

        //set 0 可以有，也可以没有；有就必须是全工程那一个 UBO，且不能读出缓冲之外
        if (frameBindingCount > 1)
        {
            throw std::runtime_error("set 0 must contain only the frame uniform buffer (binding 0)");
        }
        if (pFrameBinding != nullptr)
        {
            if (pFrameBinding->binding != DoodleDescriptorManager::kFrameUboBinding.binding ||
                pFrameBinding->descriptorType != DoodleDescriptorManager::kFrameUboBinding.descriptorType ||
                pFrameBinding->descriptorCount != 1)
            {
                throw std::runtime_error("set 0 binding 0 must be a single uniform buffer, as declared in "
                                         "DoodleDescriptorManager::kFrameUboBinding");
            }
            if ((pFrameBinding->stageFlags & ~DoodleDescriptorManager::kFrameUboBinding.stageFlags) != 0)
            {
                throw std::runtime_error("set 0 UBO is used in a shader stage the frame layout does not cover");
            }
            if (pFrameBinding->blockSize > sizeof(DoodleUniformBufferObject))
            {
                throw std::runtime_error(
                    "set 0 UBO block is " + std::to_string(pFrameBinding->blockSize) +
                    " bytes but DoodleUniformBufferObject is " + std::to_string(sizeof(DoodleUniformBufferObject)) +
                    " bytes");
            }
        }

        //材质 set 1 独立之前，set 1 的集合由描述符管理器按纹理分配；它的布局与这里
        //反射出来的布局只在定义完全相同时才兼容。所以临时要求两者逐项一致
        //（一张片元阶段的纹理），多贴图 / 材质自己的参数块落地后这条约束由材质接管
        if (materialBindings.size() != 1)
        {
            throw std::runtime_error("set 1 must contain exactly one texture binding (one combined image sampler)");
        }
        const VkDescriptorSetLayoutBinding& textureBinding = materialBindings.front();
        if (textureBinding.binding != DoodleDescriptorManager::kTextureBinding.binding ||
            textureBinding.descriptorType != DoodleDescriptorManager::kTextureBinding.descriptorType ||
            textureBinding.descriptorCount != DoodleDescriptorManager::kTextureBinding.descriptorCount ||
            textureBinding.stageFlags != DoodleDescriptorManager::kTextureBinding.stageFlags)
        {
            throw std::runtime_error("set 1 binding 0 must be a single combined image sampler used only by the "
                                     "fragment stage (see DoodleDescriptorManager::kTextureBinding)");
        }

        //push constant 只能落在全工程那一段范围之内、只用它覆盖的阶段：
        //超出范围时管线布局与录制侧的 push 对不上，校验层会在绘制时报
        for (const VkPushConstantRange& range : pushConstantRanges)
        {
            if (range.offset < DoodleVulkanPipelineObject::kPushConstantRange.offset ||
                range.offset + range.size >
                    DoodleVulkanPipelineObject::kPushConstantRange.offset + DoodleVulkanPipelineObject::kPushConstantRange.size ||
                (range.stageFlags & ~DoodleVulkanPipelineObject::kPushConstantRange.stageFlags) != 0)
            {
                throw std::runtime_error("push constants must fit in DoodleVulkanPipelineObject::kPushConstantRange");
            }
        }

        std::sort(materialBindings.begin(), materialBindings.end(),
                  [](const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs)
                  {
                      return lhs.binding < rhs.binding;
                  });
        return materialBindings;
    }

    void DoodleShader::CreateMaterialSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& materialBindings)
    {
        const VkDescriptorSetLayoutCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(materialBindings.size()),
            .pBindings = materialBindings.data()
        };

        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &materialSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create the set 1 layout");
        }

        m_forwardPassDescription.materialDescriptorSetLayout = materialSetLayout;
    }

    const char* DoodleShader::GetStageName(const VkShaderStageFlagBits stage)
    {
        switch (stage)
        {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "vertex";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "fragment";
        default:
            return "unknown";
        }
    }
}
