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

    void DoodleShader::Initialize(const DoodleShaderInitInfo& initInfo)
    {
        m_pDevice = &initInfo.device;

        //先反射与核对、再动设备：不合法的着色器不留下任何句柄。
        //任何一步失败都用 Destroy 把已经建出的东西收掉，再原样往外抛；
        //资产路径由调用方（加载器）补。清理放在函数这一层、不挪进循环：
        //那样清理对不对，就取决于循环里的 catch 会不会一直重抛
        try
        {
            //第一步：逐个 pass 反射，按 pass 契约核对 set 0 与 push constant；
            //各 pass 的 set 1 并成整个着色器的一张表，最后按材质契约核对。这一步不碰设备
            std::vector<ReflectedBinding> materialBindings;
            for (const auto& passInitInfo : initInfo.passInitInfos)
            {
                std::vector<ReflectedBinding> bindings;
                std::vector<VkPushConstantRange> pushConstantRanges;
                ReflectStage(passInitInfo.vertexBytecode, VK_SHADER_STAGE_VERTEX_BIT, bindings, pushConstantRanges);
                ReflectStage(passInitInfo.fragmentBytecode, VK_SHADER_STAGE_FRAGMENT_BIT, bindings, pushConstantRanges);

                //未知的 pass 枚举值在这里被拒：GetRenderPassLayout 只认已知的 pass
                ValidatePassBindings(GetRenderPassLayout(passInitInfo.passType), bindings, pushConstantRanges);
                MergeMaterialBindings(bindings, materialBindings);
            }
            ValidateMaterialBindings(materialBindings);

            //set 1 的布局定义：建布局用它
            std::vector<VkDescriptorSetLayoutBinding> materialLayoutBindings;
            materialLayoutBindings.reserve(materialBindings.size());
            m_materialTextureSlots.reserve(materialBindings.size());
            for (const ReflectedBinding& reflected : materialBindings)
            {
                materialLayoutBindings.push_back(
                {
                    .binding = reflected.binding,
                    .descriptorType = reflected.descriptorType,
                    .descriptorCount = reflected.descriptorCount,
                    .stageFlags = reflected.stageFlags,
                    .pImmutableSamplers = nullptr
                });
                m_materialTextureSlots.push_back({.binding = reflected.binding, .name = reflected.name});
            }

            //第二步：占位并填好各 pass 的数据。这里只有内存分配会失败，此时还没有句柄可漏。
            //字节码各拷一份留在 pass 数据里：pass 第一次解析管线时，管线记下这份数据的地址，
            //格式变化原地重建时还要读它。
            //pass 不重复由 loader 保证，这里拿到的总是新节点
            for (const auto& passInitInfo : initInfo.passInitInfos)
            {
                DoodleShaderPass& pass = m_passes.try_emplace(passInitInfo.passType).first->second;
                pass.vertexBytecode = passInitInfo.vertexBytecode;
                pass.fragmentBytecode = passInitInfo.fragmentBytecode;
                pass.renderState = passInitInfo.passRenderState;
                pass.vertexFormat = initInfo.vertexFormat;
                pass.instanceFormat = initInfo.instanceFormat;
            }

            //第三步：最后才建那一份 set 1 布局；把句柄写进每个 pass 的数据只是赋值，不会再失败。
            //建出来之后再失败（下面的投递），由 Destroy 统一收掉
            m_materialDescriptorSetLayout = CreateMaterialSetLayout(materialLayoutBindings);
            for (auto& entry : m_passes)
            {
                entry.second.materialDescriptorSetLayout = m_materialDescriptorSetLayout;
            }

            //一切状态都已就绪，此刻起可以拿这些 pass 数据向任何 pass 的管线表要管线。
            //投递给渲染侧，在下一帧取帧之前统一建出来，不落进命令录制。
            //必须是 try 里的最后一步：投递本身也可能失败（加锁、扩容），失败时照样
            //收掉布局；投出去之后就不能再失败，否则队列里会留下一个要被删掉的着色器。
            //loader 那一侧同理：DoodleAssetLoader::Load 在 Create 之前就占好缓存位置，
            //Initialize 返回之后着色器一定留在缓存里
            DoodlePipelineWarmupQueue::GetInstance().Push(*this);
        }
        catch (...)
        {
            Destroy();
            throw;
        }
    }

    void DoodleShader::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //set 1 布局可能与管线表里的管线布局、描述符管理器里的材质集合存在「出生关系」，
        //但它先于它们销毁是合法的（见类注释）。各 pass 数据里只是句柄副本，不另销毁；
        //失败在建布局之前时，这里还是 VK_NULL_HANDLE
        if (m_materialDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(), m_materialDescriptorSetLayout, nullptr);
            m_materialDescriptorSetLayout = VK_NULL_HANDLE;
        }
        m_passes.clear();
        m_materialTextureSlots.clear();

        m_pDevice = nullptr;
    }

    const DoodleShaderPass* DoodleShader::GetPass(const EDoodleRenderPassType pass) const
    {
        const auto it = m_passes.find(pass);
        if (it == m_passes.end())
        {
            return nullptr;
        }

        return &it->second;
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
            throw std::runtime_error(std::string("the ") + stageName + " bytecode is not a " + stageName + " shader");
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
            throw std::runtime_error(std::string("the ") + stageName + " shader has no entry point named 'main'");
        }

        //描述符绑定：按入口点取。同一份资源可能在两个阶段都声明（set 0 的 UBO
        //就是），所以合并到同一张表里，阶段取并集
        uint32_t bindingCount = 0;
        if (module.EnumerateEntryPointDescriptorBindings(pEntryPoint, &bindingCount, nullptr) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to enumerate ") + stageName + " descriptor bindings");
        }
        if (bindingCount > 0)
        {
            std::vector<SpvReflectDescriptorBinding*> stageBindings(bindingCount);
            if (module.EnumerateEntryPointDescriptorBindings(pEntryPoint, &bindingCount, stageBindings.data()) != SPV_REFLECT_RESULT_SUCCESS)
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

                //变量名来自 SPIR-V 的调试名（OpName）；字节码去掉了调试名时为空，
                //set 1 会在 ValidateMaterialBindings 里因此被拒
                reflected.name = pStageBinding->name != nullptr ? pStageBinding->name : "";

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
                //块大小不一致说明两边的声明已经漂移；set 1 的贴图按变量名与材质对上，
                //两个阶段起了不同的名字，材质就不知道该配给哪个 —— 这些都是内容错误，
                //不静默取其一
                if (sameBinding->descriptorType != reflected.descriptorType ||
                    sameBinding->descriptorCount != reflected.descriptorCount ||
                    sameBinding->blockSize != reflected.blockSize ||
                    (reflected.set == 1 && sameBinding->name != reflected.name))
                {
                    throw std::runtime_error(
                        "set " + std::to_string(reflected.set) + " binding " + std::to_string(reflected.binding) +
                        " is declared differently in the vertex and fragment shaders");
                }
                sameBinding->stageFlags |= expectedStage;
            }

        }
        
        //push constant：记下这个阶段用到的那一段（偏移与大小）。可能没有，也可能
        //只有部分成员；核对交给 ValidatePassBindings
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
                //花括号初始化里 enum 到 VkFlags 属于收窄转换，即使是常量也要显式转。
                //这里是运行期参数，MSVC 直接报 C2397
                .stageFlags = static_cast<VkShaderStageFlags>(expectedStage),
                .offset = pBlock->offset,
                .size = pBlock->size
            });
        }
    }

    void DoodleShader::ValidatePassBindings(const DoodleRenderPassLayout& passLayout,
                                            const std::vector<ReflectedBinding>& bindings,
                                            const std::vector<VkPushConstantRange>& pushConstantRanges)
    {
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
                //set 1 是材质贴图：按整个着色器合并之后，再按材质契约核对
                break;

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
            const VkDescriptorSetLayoutBinding& passBinding = passLayout.passUboBinding;
            if (pFrameBinding->binding != passBinding.binding ||
                pFrameBinding->descriptorType != passBinding.descriptorType ||
                pFrameBinding->descriptorCount != passBinding.descriptorCount)
            {
                throw std::runtime_error("set 0 binding 0 must be a single uniform buffer, as declared by the render pass layout");
            }
            if ((pFrameBinding->stageFlags & ~passBinding.stageFlags) != 0)
            {
                throw std::runtime_error("set 0 UBO is used in a shader stage the render pass layout does not cover");
            }
            if (pFrameBinding->blockSize > passLayout.passUboSize)
            {
                throw std::runtime_error(
                    "set 0 UBO block is " + std::to_string(pFrameBinding->blockSize) +
                    " bytes but this pass allows " + std::to_string(passLayout.passUboSize) +
                    " bytes");
            }
        }

        //push constant 只能落在本 pass 逐绘制项数据的范围之内、只用它覆盖的阶段：
        //超出范围时管线布局与录制侧的 push 对不上，校验层会在绘制时报
        for (const VkPushConstantRange& range : pushConstantRanges)
        {
            const VkPushConstantRange& passRange = passLayout.drawItemDataRange;
            if (range.offset < passRange.offset ||
                range.offset + range.size > passRange.offset + passRange.size ||
                (range.stageFlags & ~passRange.stageFlags) != 0)
            {
                throw std::runtime_error("push constants must fit in this pass's push constant range");
            }
        }
    }

    void DoodleShader::MergeMaterialBindings(const std::vector<ReflectedBinding>& passBindings,
                                             std::vector<ReflectedBinding>& materialBindings)
    {
        for (const ReflectedBinding& reflected : passBindings)
        {
            if (reflected.set != 1)
            {
                continue;
            }

            const auto sameBinding = std::find_if(materialBindings.begin(), materialBindings.end(),
                [&reflected](const ReflectedBinding& existing)
                {
                    return existing.binding == reflected.binding;
                });

            if (sameBinding == materialBindings.end())
            {
                //材质按名字配贴图：同一个名字占了两个 binding，就分不清该写进哪一个
                const auto sameName = std::find_if(materialBindings.begin(), materialBindings.end(),
                    [&reflected](const ReflectedBinding& existing)
                    {
                        return existing.name == reflected.name;
                    });
                if (sameName != materialBindings.end())
                {
                    throw std::runtime_error("set 1 texture '" + reflected.name + "' is declared at both binding " +
                                             std::to_string(sameName->binding) + " and binding " +
                                             std::to_string(reflected.binding));
                }

                materialBindings.push_back(reflected);
                continue;
            }

            //各 pass 合用一份 set 1：同一个 binding 必须是同一张贴图，阶段取并集
            if (sameBinding->name != reflected.name)
            {
                throw std::runtime_error("set 1 binding " + std::to_string(reflected.binding) + " is '" +
                                         sameBinding->name + "' in one pass and '" + reflected.name +
                                         "' in another; all passes of a shader share one set 1");
            }
            if (sameBinding->descriptorType != reflected.descriptorType ||
                sameBinding->descriptorCount != reflected.descriptorCount)
            {
                throw std::runtime_error("set 1 texture '" + reflected.name + "' is declared differently in two passes");
            }
            sameBinding->stageFlags |= reflected.stageFlags;
        }
    }

    void DoodleShader::ValidateMaterialBindings(std::vector<ReflectedBinding>& materialBindings)
    {
        if (materialBindings.size() > kMaxMaterialTextureSlots)
        {
            throw std::runtime_error("set 1 declares " + std::to_string(materialBindings.size()) +
                                     " textures; at most " + std::to_string(kMaxMaterialTextureSlots) +
                                     " are allowed");
        }

        for (const ReflectedBinding& reflected : materialBindings)
        {
            //set 1 只放贴图：描述符管理器只往里写 combined image sampler，一个绑定一张
            if (reflected.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                reflected.descriptorCount != 1)
            {
                throw std::runtime_error("set 1 binding " + std::to_string(reflected.binding) +
                                         " must be a single combined image sampler (e.g. sampler2D)");
            }

            //材质按变量名配贴图：字节码去掉了调试名就对不上
            if (reflected.name.empty())
            {
                throw std::runtime_error("set 1 binding " + std::to_string(reflected.binding) +
                                         " has no variable name; materials assign textures by name, "
                                         "so the SPIR-V must keep its debug names");
            }
        }

        //按 binding 升序：贴图槽表按这个顺序排（DoodleShader.h 的承诺），
        //建布局用的是同一份定义
        std::sort(materialBindings.begin(), materialBindings.end(),
                  [](const ReflectedBinding& lhs, const ReflectedBinding& rhs)
                  {
                      return lhs.binding < rhs.binding;
                  });
    }

    VkDescriptorSetLayout DoodleShader::CreateMaterialSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& materialBindings) const
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
