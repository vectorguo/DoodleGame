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
#include "Vulkan/DoodleVulkanDevice.h"
#include "spirv_reflect/spirv_reflect.h"

namespace Doodle
{
    DoodleShader::DoodleShader(const std::string_view assetPath)
        : DoodleAsset(assetPath)
    {
    }

    void DoodleShader::Initialize(const DoodleShaderInitInfo& info)
    {
        if (info.passSources.empty())
        {
            throw std::runtime_error("shader has no pass descriptions");
        }

        m_pDevice = &info.device;

        //先反射与核对、再动设备：不合法的着色器不留下任何句柄。
        //失败时 Destroy 会把已经建出的 per-pass 布局逐个收掉。
        //错误消息加 pass 名前缀；资产路径由调用方（加载器）补
        try
        {
            for (const DoodlePassShaderSource& source : info.passSources)
            {
                const std::size_t passIndex = static_cast<std::size_t>(source.pass);
                if (passIndex >= m_passDescriptions.size())
                {
                    throw std::runtime_error("unknown pass type");
                }
                if (m_passDescriptions[passIndex].has_value())
                {
                    throw std::runtime_error("duplicate pass description");
                }

                try
                {
                    if (source.vertexBytecode.empty())
                    {
                        throw std::runtime_error("vertex bytecode is empty");
                    }
                    if (source.fragmentBytecode.empty())
                    {
                        throw std::runtime_error("fragment bytecode is empty");
                    }

                    std::vector<ReflectedBinding> bindings;
                    std::vector<VkPushConstantRange> pushConstantRanges;
                    ReflectStage(source.vertexBytecode, VK_SHADER_STAGE_VERTEX_BIT, bindings, pushConstantRanges);
                    ReflectStage(source.fragmentBytecode, VK_SHADER_STAGE_FRAGMENT_BIT, bindings, pushConstantRanges);

                    const DoodlePassShaderInterface& passInterface = GetPassShaderInterface(source.pass);
                    const std::vector<VkDescriptorSetLayoutBinding> materialBindings =
                        ValidateInterface(passInterface, bindings, pushConstantRanges);
                    const VkDescriptorSetLayout materialSetLayout =
                        CreateMaterialSetLayout(materialBindings);

                    //字节码各拷一份留在描述里：pass 向管线表要管线时，表按内容做键并把这份
                    //描述连同字节码存进条目，格式变化原地重建时用它
                    DoodleVulkanPipelineShaderDescription description;
                    description.vertexBytecode = source.vertexBytecode;
                    description.fragmentBytecode = source.fragmentBytecode;
                    description.materialDescriptorSetLayout = materialSetLayout;
                    description.renderState = source.renderState;
                    description.vertexFormat = info.vertexFormat;
                    description.instanceFormat = info.instanceFormat;

                    //放进去之后，失败清理由 Destroy 统一负责
                    m_passDescriptions[passIndex] = std::move(description);
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error(std::string("pass '") + GetPassName(source.pass) + "': " + error.what());
                }
            }
        }
        catch (...)
        {
            Destroy();
            throw;
        }

        //一切状态都已就绪，此刻起可以拿这些描述向任何 pass 的管线表要管线。
        //投递给渲染侧，在下一帧取帧之前统一建出来，不落进命令录制
        DoodlePipelineWarmupQueue::GetInstance().Push(*this);
    }

    void DoodleShader::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //set 布局可能与管线表里的管线布局存在「出生关系」，但那条依赖只在创建
        //管线布局时读过一次 —— 本资产先于管线表销毁是合法的（见类注释）。
        //逐个槽位走：部分 pass 初始化失败时也要把前面建出的布局收掉
        for (std::optional<DoodleVulkanPipelineShaderDescription>& description : m_passDescriptions)
        {
            if (!description.has_value())
            {
                continue;
            }

            if (description->materialDescriptorSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(),
                                             description->materialDescriptorSetLayout,
                                             nullptr);
            }

            description.reset();
        }

        m_pDevice = nullptr;
    }

    const DoodleVulkanPipelineShaderDescription* DoodleShader::GetPassDescription(const EDoodlePassType pass) const
    {
        const std::size_t passIndex = static_cast<std::size_t>(pass);
        if (passIndex >= m_passDescriptions.size() || !m_passDescriptions[passIndex].has_value())
        {
            return nullptr;
        }

        return &*m_passDescriptions[passIndex];
    }

    void DoodleShader::ReflectStage(const std::vector<std::byte>& bytecode,
                                    const VkShaderStageFlagBits expectedStage,
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
            //这个 pass 的常量块
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
        const DoodlePassShaderInterface& passInterface,
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
            {
                //set 1 的定义必须与这个 pass 允许的那一份逐项一致（含 stageFlags）：
                //DoodleDescriptorManager 按同一份定义分配材质集合，定义不同就不兼容
                const auto allowed = std::find_if(
                    passInterface.materialBindings.begin(),
                    passInterface.materialBindings.end(),
                    [&reflected](const VkDescriptorSetLayoutBinding& binding)
                    {
                        return binding.binding == reflected.binding &&
                               binding.descriptorType == reflected.descriptorType &&
                               binding.descriptorCount == reflected.descriptorCount &&
                               binding.stageFlags == reflected.stageFlags;
                    });
                if (allowed == passInterface.materialBindings.end())
                {
                    throw std::runtime_error("set 1 binding " + std::to_string(reflected.binding) +
                                             " is not allowed by this pass");
                }

                materialBindings.push_back(
                {
                    .binding = reflected.binding,
                    .descriptorType = reflected.descriptorType,
                    .descriptorCount = reflected.descriptorCount,
                    .stageFlags = reflected.stageFlags,
                    .pImmutableSamplers = nullptr
                });
                break;
            }

            default:
                //set 0 是本 pass 的常量；set 1 是材质资源。第三个集合没有宿主，
                //声明了也没有人能给它分配集合
                throw std::runtime_error("set " + std::to_string(reflected.set) +
                                         " is not supported (only set 0 and set 1 are)");
            }
        }

        //set 0 可以有，也可以没有；有就必须是这个 pass 的 UBO，且不能读出缓冲之外
        if (frameBindingCount > 1)
        {
            throw std::runtime_error("set 0 must contain only the pass uniform buffer (binding 0)");
        }
        if (pFrameBinding != nullptr)
        {
            const VkDescriptorSetLayoutBinding& passBinding = passInterface.passUboBinding;
            if (pFrameBinding->binding != passBinding.binding ||
                pFrameBinding->descriptorType != passBinding.descriptorType ||
                pFrameBinding->descriptorCount != passBinding.descriptorCount)
            {
                throw std::runtime_error("set 0 binding 0 must be a single uniform buffer, as declared by the pass shader interface");
            }
            if ((pFrameBinding->stageFlags & ~passBinding.stageFlags) != 0)
            {
                throw std::runtime_error("set 0 UBO is used in a shader stage the pass interface does not cover");
            }
            if (pFrameBinding->blockSize > passInterface.maxPassUboBlockSize)
            {
                throw std::runtime_error(
                    "set 0 UBO block is " + std::to_string(pFrameBinding->blockSize) +
                    " bytes but this pass allows " + std::to_string(passInterface.maxPassUboBlockSize) +
                    " bytes");
            }
        }

        //forward 要求恰好一张材质纹理；shadow 允许零张或一张。
        //有多条允许绑定时逐个核对，每个都必须恰好出现一次
        if (passInterface.materialBindingsRequired)
        {
            for (const VkDescriptorSetLayoutBinding& allowed : passInterface.materialBindings)
            {
                const auto count = std::count_if(
                    materialBindings.begin(),
                    materialBindings.end(),
                    [&allowed](const VkDescriptorSetLayoutBinding& binding)
                    {
                        return binding.binding == allowed.binding;
                    });
                if (count != 1)
                {
                    throw std::runtime_error("set 1 must contain binding " + std::to_string(allowed.binding) +
                                             " for this pass");
                }
            }
        }

        //push constant 只能落在本 pass 那一段范围之内、只用它覆盖的阶段：
        //超出范围时管线布局与录制侧的 push 对不上，校验层会在绘制时报
        for (const VkPushConstantRange& range : pushConstantRanges)
        {
            const VkPushConstantRange& passRange = passInterface.pushConstantRange;
            if (range.offset < passRange.offset ||
                range.offset + range.size > passRange.offset + passRange.size ||
                (range.stageFlags & ~passRange.stageFlags) != 0)
            {
                throw std::runtime_error("push constants must fit in this pass's push constant range");
            }
        }

        std::sort(materialBindings.begin(), materialBindings.end(),
                  [](const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs)
                  {
                      return lhs.binding < rhs.binding;
                  });
        return materialBindings;
    }

    VkDescriptorSetLayout DoodleShader::CreateMaterialSetLayout(
        const std::vector<VkDescriptorSetLayoutBinding>& materialBindings) const
    {
        if (materialBindings.empty())
        {
            return VK_NULL_HANDLE;
        }

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

        return materialSetLayout;
    }

    const char* DoodleShader::GetPassName(const EDoodlePassType pass)
    {
        switch (pass)
        {
        case EDoodlePassType::Forward:
            return "forward";

        case EDoodlePassType::Shadow:
            return "shadow";
        }

        return "unknown";
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
