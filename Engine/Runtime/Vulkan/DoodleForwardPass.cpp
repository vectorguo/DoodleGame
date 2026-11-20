//
//  DoodleForwardPass.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#include "DoodleForwardPass.h"

#include <stdexcept>

#include "DoodleDepthImages.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanUtils.h"
#include "Rendering/DoodleCamera.h"
#include "Rendering/DoodleForwardPassData.h"
#include "Rendering/DoodleFrameContext.h"
#include "Rendering/DoodleLight.h"
#include "Rendering/DoodleMaterial.h"
#include "Rendering/DoodleMesh.h"
#include "Rendering/DoodleShader.h"

namespace Doodle
{
    void DoodleForwardPass::Initialize(const DoodleForwardPassInitInfo& info)
    {
        m_pSwapChain = &info.swapChain;
        m_pDepthImages = &info.depthImages;

        //set 0 与 push 范围由基类按 forward 的契约自己取；这里只给附件配置
        DoodleRenderPass::Initialize(info.renderPassInfo, MakePassConfig(info.swapChain, info.depthImages));
    }

    void DoodleForwardPass::UpdatePassUbo(const DoodleFrameContext& context)
    {
        DoodleForwardPassUbo ubo;

        //view 由相机从宿主 Transform 现算：相机不存位置 / 朝向，
        //移动它所在的对象，画面自然跟着变
        ubo.view = context.camera.GetViewMatrix();

        //每帧重新读交换链尺寸：窗口缩放后宽高比自动正确，
        //描述符集与 UBO 对象本身都不需要重建
        const auto extent = m_pSwapChain->GetImageExtent();
        const auto preTransform = m_pSwapChain->GetPreTransform();

        //宽高比要用**用户看到的那一组**。交换链做了预旋转时，图像宽高和显示方向是对调的，
        //所以这里再对调回来 —— 透视必须按显示方向算，算完再转，才是同一份画面。
        //
        //Y 翻转已经在 GetProjectionMatrix 里做完；这里拿到的 proj 是「显示方向 +
        //Vulkan 裁剪空间」的结果
        ubo.proj = context.camera.GetProjectionMatrix(DoodleVulkanUtils::GetDisplayAspect(extent, preTransform));

        //最后把整幅画面转到图像自己的朝向上。桌面拿到的是 IDENTITY，这一乘是单位阵。
        //
        //必须排在 Y 翻转之后、作为投影的最后一步左乘：Y 翻转负责换成 Vulkan 的 NDC，
        //预旋转是在 Vulkan 的 NDC 里做的。顺序一换，Android 上会静默变形
        ubo.proj = DoodleVulkanUtils::MakePreRotation(preTransform) * ubo.proj;

        //主光源：方向取组件宿主 Transform 的前向，颜色乘上强度。
        //方向已经是单位向量（DoodleLight::GetDirection 负责），这里不再归一化
        //
        //没有启用的光源时写零颜色：片元着色器只剩环境项，画面偏暗但仍是
        //同一份着色逻辑。方向填一个固定值只是让 UBO 内容确定，不参与可见结果
        if (context.pLight != nullptr)
        {
            ubo.lightDirection = glm::vec4(context.pLight->GetDirection(), 0.0f);
            ubo.lightColor = glm::vec4(context.pLight->GetColor() * context.pLight->GetIntensity(), 0.0f);
        }
        else
        {
            ubo.lightDirection = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            ubo.lightColor = glm::vec4(0.0f);
        }

        //HOST_COHERENT 保证不需要手动 flush；下一次 vkQueueSubmit 之后再读取时，
        //这份写入对 GPU 可见。写入位置已在调用处保证：帧栅栏之后、录制之前
        WritePassUbo(context.frameIndex, &ubo, sizeof(ubo));
    }

    void DoodleForwardPass::Render(const DoodleFrameContext& frameContext,
                                   const DoodleRenderPassContext& passContext)
    {
        //材质描述符集与绘制项必须一一对应。检查放在录制之前：录到一半抛异常
        //会把命令缓冲留在 recording 状态，调用方随后 end 会撞 VUID
        if (passContext.materialDescriptorSets.size() != frameContext.drawItems.size())
        {
            throw std::invalid_argument("material descriptor sets must match the number of draw items");
        }

        //本帧要画的每个材质都必须有 forward 描述，且管线要在录任何命令之前解析好。
        //缺失或建管线失败都停在这里，避免 vkCmdBeginRendering 之后才抛
        for (const DoodleDrawItem& drawItem : frameContext.drawItems)
        {
            const DoodleShader* pShader = drawItem.pMaterial->GetShader();
            if (pShader == nullptr)
            {
                throw std::logic_error("draw item material has no shader");
            }

            static_cast<void>(ResolvePipeline(*pShader));
        }

        //在录任何命令之前，把本帧的相机 / 光源写进基类的 per-pass UBO。
        //调用位置由 DoodleRenderPipeline 保证在 BeginFrame 的帧栅栏之后
        UpdatePassUbo(frameContext);

        const VkCommandBuffer commandBuffer = frameContext.commandBuffer;
        const VkExtent2D imageExtent = m_pSwapChain->GetImageExtent();
        const VkDescriptorSet passUboDescriptorSet = GetPassUboDescriptorSet(frameContext.frameIndex);

        //取图之后、开画之前：颜色与深度各转一次布局。
        //原来是渲染通道入口自动做的（连同转换时机），动态渲染之后两件事都归 pass
        RecordImageTransitionToColorAttachment(frameContext);
        RecordImageTransitionToDepthAttachment(frameContext);

        //开始渲染。原来要指一个 VkRenderPass 加一个 VkFramebuffer，现在改成把
        //这批结构直接当参数写出来：视图、布局、load/store、清屏值。
        //
        //注意 imageLayout 是陈述句 ——「这张图**现在**处于什么布局」，不是转换请求。
        //上面那条屏障把它转成什么，这里就必须写什么；写成别的布局是未定义行为，
        //而且常见表现是「在自家驱动上碰巧能跑」
        const VkRenderingAttachmentInfo colorAttachment
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_pSwapChain->GetImageViews()[frameContext.imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            //VkClearValue 是 union，逐层指定：VkClearValue → .color → .float32
            .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}}
        };

        //深度附件。与颜色附件同一套写法：视图、当前布局、load/store、清屏值。
        //注意这里没有「新建一张图」的语义 —— 图已经在深度图组里建好，
        //本函数只是把它登记成这次渲染的深度目标
        const VkRenderingAttachmentInfo depthAttachment
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_pDepthImages->GetImageView(frameContext.frameIndex),
            //这句是陈述句：必须是上面那条屏障转换出来的布局，写错了是未定义行为
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            //画完就没人看它了。瓦片式硬件（Apple GPU 就是这样）会因此
            //根本不把深度写回显存 —— 这是白拿的一笔带宽
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            //1.0 = 最远（Vulkan 的深度范围是 0..1，1 在远平面）：
            //任何片段都比它近，第一笔画的东西必然通过
            .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}}
        };

        const VkRenderingInfo renderingInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea =
            {
                .offset = {0, 0},
                //渲染区域应与附件尺寸一致，不一致时驱动要额外处理边界，会变慢
                .extent = imageExtent
            },
            .layerCount = 1,   //图像数组的层数，与 imageArrayLayers 一致，不是图像张数
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment
        };

        //开始渲染。begin 与 end 必须成对，且落在同一个命令缓冲里
        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        //视口与剪裁在管线层被声明为动态状态，没有烘焙进管线，只能在这里补上。
        //交换链尺寸变化时只影响这里，不用重建管线
        const VkViewport viewport
        {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(imageExtent.width),
            .height = static_cast<float>(imageExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        const VkRect2D scissor
        {
            .offset = {0, 0},
            .extent = imageExtent
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        //逐条绘制项录绘制。一条绘制项对应一次索引绘制：网格与子网格区间
        //已经在收集侧展开好，这里只做随绘制项变化的几件事：
        //换管线、绑材质贴图（set 1）、推 localToWorld + 材质基色、
        //绑网格的顶点 / 索引缓冲、按区间发一次绘制
        const DoodleVulkanPipelineObject* pBoundPipeline = nullptr;
        bool frameSetBound = false;
        VkDescriptorSet boundMaterialSet = VK_NULL_HANDLE;
        const VkPushConstantRange pushConstantRange = GetPushConstantRange();
        for (std::size_t i = 0; i < frameContext.drawItems.size(); ++i)
        {
            const DoodleDrawItem& drawItem = frameContext.drawItems[i];

            //网格给出的是顶点 / 索引缓冲句柄；子网格按下标回网格取（表在
            //Initialize 之后不可变），材质是收集时从 renderer 解析好的
            //非拥有指针。本函数不认识网格或材质是什么，只把这些数录进命令里
            const auto& mesh = *drawItem.pMesh;
            const auto& submesh = mesh.GetSubmesh(drawItem.submeshIndex);
            const auto& material = *drawItem.pMaterial;

            //管线按材质的着色器解析：着色器资产常驻，同一份描述只会有一条管线；
            //memo 命中后这里只是一次指针查表。同一条管线会连续出现在多个材质上，
            //只有变了才重绑
            const DoodleShader* pShader = material.GetShader();
            if (pShader == nullptr)
            {
                throw std::logic_error("draw item material has no shader");
            }
            const DoodleVulkanPipelineObject* pPipeline = &ResolvePipeline(*pShader);
            if (pPipeline != pBoundPipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pPipeline->GetGraphicsPipeline());
                pBoundPipeline = pPipeline;

                //换管线后 set 1 必须重绑：贴图有几张由着色器决定，各个着色器的 set 1
                //布局不同，之前绑的那个集合不一定能被新管线解释
                boundMaterialSet = VK_NULL_HANDLE;
            }

            //set 0 整帧只绑一次：本 pass 内所有管线共用同一个 set 0 布局与 push 范围，
            //换管线不会让它失效。用当前管线的布局来解释它
            if (!frameSetBound)
            {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pPipeline->GetPipelineLayout(),
                    0,
                    1,
                    &passUboDescriptorSet,
                    0,
                    nullptr);
                frameSetBound = true;
            }

            //绑定本项的材质贴图（set 1）。集合由描述符管理器按材质缓存好，与绘制项同下标。
            //连续多个子网格共用同一个材质时，句柄相同，跳过重复绑定。
            //着色器没有声明贴图时集合是 VK_NULL_HANDLE：它的管线布局里没有 set 1，不绑
            const VkDescriptorSet materialDescriptorSet = passContext.materialDescriptorSets[i];
            if (materialDescriptorSet != VK_NULL_HANDLE && materialDescriptorSet != boundMaterialSet)
            {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pPipeline->GetPipelineLayout(),
                    1,
                    1,
                    &materialDescriptorSet,
                    0,
                    nullptr);
                boundMaterialSet = materialDescriptorSet;
            }

            //localToWorld 与材质基色每个绘制项一份，走 push constant。数据块的布局
            //由 DoodleForwardPassPushConstants 钉住，阶段 / 偏移 / 大小取本 pass 的
            //pushConstantRange —— 管线布局与这里只认同一份定义，改一处不会漏另一处
            //
            //基色在写进 push constant 的这一处才从作者写的 sRGB 转成线性：
            //纹理按 SRGB 格式上传，采样结果本来就是线性，两者相乘才在同一空间
            //
            //push 的是这条命令缓冲的当前值，下一条 push 直接覆盖 ——
            //不需要每绘制项一块 UBO，也不需要等 GPU 用完上一份
            const DoodleForwardPassPushConstants pushConstants
            {
                .localToWorld = drawItem.localToWorld,
                .baseColor = material.GetBaseColor().SRGBToLinear().ToVec4()
            };
            vkCmdPushConstants(
                commandBuffer,
                pPipeline->GetPipelineLayout(),
                pushConstantRange.stageFlags,
                pushConstantRange.offset,
                pushConstantRange.size,
                &pushConstants);

            //绑定顶点缓冲。这里的 binding 编号 0 指回管线顶点输入描述里的 binding = 0 ——
            //所以它必须在 vkCmdBindPipeline 之后：管线还没绑定时，这个编号无从对应。
            //绑定是录制状态，对之后录进来的每一次绘制都生效，所以每条绘制项各绑一次。
            //
            //当前每个网格只有一条顶点数据流（bindingCount = 1），用单变量取地址即可；
            //vkCmdBindVertexBuffers 本身收的是数组首地址，以后多 mesh / 多数据流时
            //再换回顶点缓冲数组，调用形状不用变
            const VkBuffer vertexBuffer = mesh.GetVertexBuffer();
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);

            //绑定索引缓冲。与上面那行有三处不一样：
            //
            //① 没有 binding 编号。索引缓冲全场只有一个槽位，不像顶点缓冲能同时绑好几条
            //   数据流按编号取用 —— 所以这个函数收的是单个句柄，不是数组
            //② 要多给一个索引类型。VkBuffer 只是一段字节，它自己不知道里面装的是 16 位
            //   还是 32 位的索引，这个信息没有别处能推出来，只能在这里说
            //③ 这里那个 0 是缓冲内的**字节偏移**，不是索引下标。要让绘制从第几个索引
            //   开始，那是下面 firstIndex 的事，两者别混
            //
            //索引类型写死成 UINT32：DoodleMesh 的索引数组就是 uint32_t。
            //项目里所有网格（当前只有 FBX 拍平结果）共用这一种索引类型，
            //不做「小模型用 16 位、大模型用 32 位」的分叉 —— 那会让
            //DoodleMesh 与这里各带一份分支状态
            vkCmdBindIndexBuffer(commandBuffer, mesh.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            //按区间绘制。indexCount 与 firstIndex 来自子网格：同一份索引缓冲按材质
            //切成若干段，换段只换这两个数，不必为每段另建缓冲或重新上传。
            //
            //后两个参数 vertexOffset / firstInstance 仍为 0：子网格共用同一份顶点
            //缓冲，顶点偏移已经由索引值本身表达；本项目也不做实例化
            vkCmdDrawIndexed(commandBuffer,
                             submesh.indexCount,
                             1,
                             submesh.firstIndex,
                             0,
                             0);
        }

        //结束渲染
        vkCmdEndRendering(commandBuffer);

        //画完、交回呈现引擎之前：转回呈现布局 —— 原来是渲染通道 finalLayout 的活
        RecordImageTransitionToPresent(frameContext);
    }

    void DoodleForwardPass::RebuildIfPassConfigChanged(const DoodleSwapChain& swapChain,
                                                       const DoodleDepthImages& depthImages)
    {
        const DoodleVulkanPipelinePassConfig passConfig = MakePassConfig(swapChain, depthImages);
        if (passConfig != GetPassConfig())
        {
            RebuildPipelines(passConfig);
        }
    }

    DoodleVulkanPipelinePassConfig DoodleForwardPass::MakePassConfig(const DoodleSwapChain& swapChain,
                                                                    const DoodleDepthImages& depthImages)
    {
        return
        {
            .colorFormats = {swapChain.GetImageFormat()},
            .depthFormat = depthImages.GetFormat(),
            .sampleCount = VK_SAMPLE_COUNT_1_BIT
        };
    }

    void DoodleForwardPass::RecordImageTransitionToColorAttachment(const DoodleFrameContext& frameContext) const
    {
        //取图之后、开画之前。
        //
        //这一步原来是渲染通道的活：附件描述里 initialLayout 到 COLOR_ATTACHMENT_OPTIMAL 的
        //转换由驱动在通道入口自动做，那条 VkSubpassDependency 只是把转换时机从 TOP_OF_PIPE
        //推迟到颜色附件输出阶段。动态渲染不做任何自动转换，于是「转换」与「时机」两件事都
        //落到这条屏障上 —— 六格取值就是把那条依赖原样翻译过来。
        //sync2 之后，两个 stage 与两个 access 都住进结构体本身：旧式里它们一半在结构体、
        //一半挂在 vkCmdPipelineBarrier 的调用上，「src 的四个在哪儿、dst 的四个在哪儿」
        //不再需要回答
        //oldLayout 取 UNDEFINED 而不是 PRESENT_SRC_KHR：后者等于声明「上一帧结尾把它转成了
        //呈现布局」，于是录制开始依赖既往状态，首帧与重建后各要一套说法。UNDEFINED 是
        //「旧内容不要了」，反正紧接着就要清屏 —— 每帧从同一条路重算期望态，三条路径收敛
        //
        //srcAccessMask 取 NONE：上一次碰这张图的是呈现引擎的读，它不在这个内存模型里，
        //access mask 表达不了它 —— 我们要的是执行顺序，不是「谁写过它」的可见性。
        //dstAccessMask 则是被保护的具体操作：接下来对颜色附件的写
        //
        //两个 stage 都取 COLOR_ATTACHMENT_OUTPUT：src 侧必须与 DoodleVulkanManager::EndFrame 里等待信号量的
        //stageMask 一致，转换才会排在 acquire 信号量被等待之后 —— 否则呈现引擎可能还在读
        //这张图，这时动它的布局就是数据竞争；dst 侧是接下来要写它的阶段
        const VkImageMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            //不转移队列族所有权：同族时交换链用 EXCLUSIVE、异族时用 CONCURRENT
            //（见 DoodleSwapChain.cpp），两条路都不需要转移
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_pSwapChain->GetImages()[frameContext.imageIndex],
            //整张图、单层单级：交换链图像没有 mip 层级，也不是数组
            .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        //sync2 的入口：一批屏障收进一个 VkDependencyInfo。
        //memory 与 buffer 两栏留空 —— 本次只提交图像屏障（旧式里那是 vkCmdPipelineBarrier
        //那一长串参数，现在各自成栏，不用填的自然不用出现）
        const VkDependencyInfo dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(frameContext.commandBuffer, &dependencyInfo);
    }

    void DoodleForwardPass::RecordImageTransitionToDepthAttachment(const DoodleFrameContext& frameContext) const
    {
        //取图之后、开画之前。这一步在渲染通道时代由驱动在通道入口自动完成；
        //动态渲染把它交还给录制者 —— 原文标注为「可以跳过」的那一节，在这里是必做
        //每帧都从 UNDEFINED 起算，与交换链图像那条同一个写法：
        //旧内容反正紧接着就被清屏覆盖，重算期望态比依赖上一帧留下的布局更不容易出错
        //
        //切面按格式算：候选表落到带模板的格式时，模板分量必须一起声明
        const VkImageAspectFlags aspectMask =
            DoodleDepthImages::HasStencilComponent(m_pDepthImages->GetFormat())
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                : VK_IMAGE_ASPECT_DEPTH_BIT;

        //源 scope 留空：这张图上一次被用的是同一槽位的上一轮（k-2 帧），
        //而 DoodleVulkanManager::BeginFrame 开头等本槽位栅栏时，它必定已经执行完了；k-1 帧用的是另一张图。
        //所以这条屏障只负责转布局，不再负责串两帧。NONE 是 sync2 里
        //「源 scope 为空」的正当写法（见《同步的现代写法》）
        //
        //目的地是本帧的深度测试：读（比较）+ 写（通过时写回）。
        //两个 stage 都写，是因为测试落在哪个阶段取决于着色器有没有声明
        //early fragment tests —— 与原文那条 dependency 两个阶段都写同理
        const VkImageMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                          | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                           | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            //按 frameIndex 取本帧那一张：本层不缓存上游句柄，重建后重录拿到的自然是新的
            .image = m_pDepthImages->GetImage(frameContext.frameIndex),
            .subresourceRange =
            {
                .aspectMask = aspectMask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        const VkDependencyInfo dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(frameContext.commandBuffer, &dependencyInfo);
    }

    void DoodleForwardPass::RecordImageTransitionToPresent(const DoodleFrameContext& frameContext) const
    {
        //画完、交回呈现引擎之前。这一步原来是渲染通道 finalLayout 的活。
        //
        //队列族与范围同入口那条：不转移所有权、整张图单层单级
        //
        //srcAccessMask 用 COLOR_ATTACHMENT_WRITE 把刚画的颜色推到可见：转布局本身要动
        //这份数据，写成 NONE 的话呈现可能读到半成品（撕裂、花屏、偶发的半帧）。
        //这一格与入口那条的 NONE 不对称是刻意的 —— 出口这次源 scope 里真的有写。
        //dstAccessMask 取 NONE：接手的是呈现引擎，它不吃内存可见性这套，只认执行序
        //
        //dstStageMask 取 NONE：后面没有别的阶段要等 —— 转换只需在提交结束之前完成，
        //而 renderFinished 信号量正是在批次末尾 signal 的，跨引擎那一段由它兜底。
        //
        //旧式里这一格写的是 BOTTOM_OF_PIPE，那是「旧式没有空阶段这个值」时的替身写法；
        //sync2 补上了 NONE，「后面没有任何阶段」终于有了正当写法。
        //这也是整个迁移里唯一一处不是换名字的格
        const VkImageMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_pSwapChain->GetImages()[frameContext.imageIndex],
            .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        const VkDependencyInfo dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(frameContext.commandBuffer, &dependencyInfo);
    }
}
