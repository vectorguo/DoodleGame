//
// Created by 郭智 on 2026/10/7.
//

#include <stdexcept>

#include "DoodleCommandBuffer.h"
#include "DoodleDepthImages.h"
#include "DoodleGraphicsPipeline.h"
#include "DoodleMesh.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanConfig.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleCommandBuffer* DoodleCommandBuffer::s_pInstance = nullptr;

    DoodleCommandBuffer::DoodleCommandBuffer()
    {
        //与 DoodleApplication 同一个套路：谁被构造出来谁就是「当前那一份」。
        //本工程由 DoodleVulkanManager 按值持有一份，所以这里只会登记一次
        s_pInstance = this;
    }

    DoodleCommandBuffer::~DoodleCommandBuffer()
    {
        //只注销自己 —— 万一将来真的出现第二份，别把别人的登记顺手抹掉
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }
    }

    DoodleCommandBuffer* DoodleCommandBuffer::GetInstance()
    {
        return s_pInstance;
    }

    void DoodleCommandBuffer::Initialize(const DoodleVulkanDevice& device,
                                         const DoodleSwapChain& swapChain,
                                         const DoodleGraphicsPipeline& graphicsPipeline,
                                         const DoodleDepthImages& depthImages)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;
        m_pGraphicsPipeline = &graphicsPipeline;
        m_pDepthImages = &depthImages;

        //命令缓冲从池中分配，池必须先建出来
        CreateCommandPool();
        CreateCommandBuffers();

        //两个池都锚在程序生命周期上，在这里一次建好。
        //传输池必须赶在门面第一次上传之前 —— 那条顺序写在门面的 Initialize 里
        CreateTransferCommandPool();
    }

    void DoodleCommandBuffer::Destroy()
    {
        DestroyTransferCommandPool();
        DestroyCommandPool();
    }

    void DoodleCommandBuffer::CreateCommandPool()
    {
        //池绑定的队列族必须与将来提交命令的队列一致。这里录的是绘制命令，
        //将来提交到图形队列，所以取 graphicsFamilyIndex。
        //多数桌面显卡上图形族与呈现族是同一个，填错了不会立刻暴露，
        //换到两个族不重合的设备上才会出问题
        const auto familyIndex = m_pDevice->GetQueueFamilySelection().graphicsFamilyIndex.value();

        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        //每帧都要在同一个缓冲上重录，需要能单独重置
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = familyIndex;

        const auto result = vkCreateCommandPool(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_commandPool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void DoodleCommandBuffer::DestroyCommandPool()
    {
        //池引用了逻辑设备，必须先于设备层销毁
        vkDestroyCommandPool(m_pDevice->GetLogicalDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;

        //缓冲随池一起释放了，这里只是把句柄数组清空，不要再去逐个销毁
        m_commandBuffers.clear();
    }

    void DoodleCommandBuffer::CreateCommandBuffers()
    {
        //每个在途帧一份，用帧索引轮转。数量不取交换链图像数：
        //在途帧数决定「能提前录几帧」，图像数决定「能同时在屏几帧」，两者互不相干
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        //主级缓冲才能提交到队列，次级只能被主级调用
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        //一次分配一批，直接写进 vector 的底层数组
        const auto result = vkAllocateCommandBuffers(m_pDevice->GetLogicalDevice(), &allocInfo, m_commandBuffers.data());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void DoodleCommandBuffer::CreateTransferCommandPool()
    {
        //队列族与绘制池取同一个：拷贝也提交到图形队列。
        //传输队列（VK_QUEUE_TRANSFER_BIT）是另一条优化路，本项目不走
        const auto familyIndex = m_pDevice->GetQueueFamilySelection().graphicsFamilyIndex.value();

        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        //告诉驱动这个池里的缓冲寿命很短，会被很快重置或释放。
        //这是关于用法的声明，不是语义开关 —— 它不改变任何状态规则，
        //驱动拿它来挑池的分配策略（比如整池一起分配、不为此做长期规划）
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        //注意这里没有 RESET_COMMAND_BUFFER_BIT，和有绘制缓冲的那个池不一样。
        //那个位决定「能不能重置池里的单个缓冲」，而本池的缓冲根本不需要重置 ——
        //每次用完就地释放（见 DoodleGraphicsBuffer::CopyTo），下一次是全新分配、天然处于 initial 状态。
        //加了这个位反而会让「begin 会隐式重置」这条规矩在这里也成立，
        //从而掩盖掉某天有人试着复用一个旧句柄时的错误
        createInfo.queueFamilyIndex = familyIndex;

        const auto result = vkCreateCommandPool(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_transferCommandPool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create transfer command pool!");
        }
    }

    void DoodleCommandBuffer::DestroyTransferCommandPool()
    {
        //池引用了逻辑设备，必须先于设备层销毁
        vkDestroyCommandPool(m_pDevice->GetLogicalDevice(), m_transferCommandPool, nullptr);
        m_transferCommandPool = VK_NULL_HANDLE;
    }

    VkCommandBuffer DoodleCommandBuffer::Record(const uint32_t frameIndex,
                                                const uint32_t imageIndex,
                                                const std::vector<DoodleDrawItem>& drawItems,
                                                VkDescriptorSet descriptorSet)
    {
        //录制期间用到的句柄全部现取，本层不缓存副本，
        //这样交换链重建之后重录，拿到的就是新句柄
        const auto imageExtent = m_pSwapChain->GetImageExtent();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;                    //不承诺任何特殊用法
        beginInfo.pInheritanceInfo = nullptr;   //只对次级有意义，主级必须为空

        //本帧录在哪一条：按帧索引从池里那几条中取。现取而不缓存，
        //与上面 imageExtent 的理由相同 —— 本层不攥着任何上游状态的副本。
        //录完会把它交回给调用方提交，见函数末尾
        const auto commandBuffer = m_commandBuffers[frameIndex];

        //对已录制过的缓冲调用 begin 会隐式重置它，命令缓冲不能追加只能重录
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        //① 取图之后、开画之前：转成能当颜色附件用的布局。
        //原来是渲染通道入口自动做的（连同转换时机），动态渲染之后两件事都归这个函数
        RecordImageTransitionToColorAttachment(commandBuffer, imageIndex);

        //①' 深度图也转一次：UNDEFINED → DEPTH_STENCIL_ATTACHMENT_OPTIMAL。
        //它只转布局；跨帧的复用安全由帧栅栏保证，见该函数里的注释
        RecordImageTransitionToDepthAttachment(commandBuffer, frameIndex);

        //② 开始渲染。原来要指一个 VkRenderPass 加一个 VkFramebuffer，现在改成把
        //这批结构直接当参数写出来：视图、布局、load/store、清屏值。
        //
        //注意 imageLayout 是陈述句 ——「这张图**现在**处于什么布局」，不是转换请求。
        //上面那条屏障把它转成什么，这里就必须写什么；写成别的布局是未定义行为，
        //而且常见表现是「在自家驱动上碰巧能跑」
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_pSwapChain->GetImageViews()[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        //VkClearValue 是 union，逐层聚合初始化：VkClearValue → .color → float32[4]，三重大括号不能少
        colorAttachment.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

        //深度附件。与颜色附件同一套写法：视图、当前布局、load/store、清屏值。
        //注意这里没有「新建一张图」的语义 —— 图已经在深度图组里建好，
        //本函数只是把它登记成这次渲染的深度目标
        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        //按 frameIndex 取本帧那一张，不缓存 —— 本层「不缓存上游句柄」那条不变量
        depthAttachment.imageView = m_pDepthImages->GetImageView(frameIndex);
        //这句是陈述句：必须是上面那条屏障转换出来的布局，写错了是未定义行为
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        //画完就没人看它了。瓦片式硬件（Apple GPU 就是这样）会因此
        //根本不把深度写回显存 —— 这是白拿的一笔带宽
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        //1.0 = 最远（Vulkan 的深度范围是 0..1，1 在远平面）：
        //任何片段都比它近，第一笔画的东西必然通过
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        //渲染区域应与附件尺寸一致，不一致时驱动要额外处理边界，会变慢
        renderingInfo.renderArea.extent = imageExtent;
        renderingInfo.layerCount = 1;   //图像数组的层数，与 imageArrayLayers 一致，不是图像张数
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        //开始渲染。begin 与 end 必须成对，且落在同一个命令缓冲里
        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        //绑定管线
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->GetGraphicsPipeline());

        //视口与剪裁在管线层被声明为动态状态，没有烘焙进管线，只能在这里补上。
        //交换链尺寸变化时只影响这里，不用重建管线
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(imageExtent.width);
        viewport.height = static_cast<float>(imageExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = imageExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        //绑定描述符集。与顶点/索引缓冲不同，描述符集绑定必须说明是给图形管线用的
        //（计算管线是另一个 bind point），而且必须用创建管线时那份管线布局来解释它。
        //firstSet = 0 / setCount = 1：本项目的管线布局只有 set 0 一个槽位。
        //集合里的 binding 0 指向当前帧的 UBO（view / proj），着色器按
        //(set = 0, binding = 0) 读取
        //
        //整帧只绑一次：组里所有对象共用同一份相机数据，没有「每个对象一个集合」
        //这种话。将来加材质纹理时，材质属于自己的描述符集，那才会在循环里重绑
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pGraphicsPipeline->GetPipelineLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr);

        //逐个对象录绘制。循环里只做随对象变化的三件事：
        //推 model、绑该对象的顶点 / 索引缓冲、发一次索引绘制
        for (const DoodleDrawItem& drawItem : drawItems)
        {
            //model 每个对象一份，走 push constant。
            //stage 与大小引用管线布局那边的同一组常量：范围是那边声明的，
            //这里只是照着填数据。两边各写一个字面量 64 的话，改一处就会
            //出现「范围 64、推了 128」这种只在绘制时报的错
            //
            //push 的是这条命令缓冲的当前值，下一条 push 直接覆盖 ——
            //不需要每对象一块 UBO，也不需要等 GPU 用完上一份
            vkCmdPushConstants(
                commandBuffer,
                m_pGraphicsPipeline->GetPipelineLayout(),
                DoodleGraphicsPipeline::ModelPushConstantStage,
                0,
                DoodleGraphicsPipeline::ModelPushConstantSize,
                &drawItem.model);

            //网格给出的是顶点 / 索引缓冲句柄与索引条数。
            //本函数不认识网格是什么，只把它们录进命令里
            const DoodleMesh& mesh = *drawItem.pMesh;

            //绑定顶点缓冲。这里的 binding 编号 0 指回管线顶点输入描述里的 binding = 0 ——
            //所以它必须在 vkCmdBindPipeline 之后：管线还没绑定时，这个编号无从对应。
            //绑定是录制状态，对之后录进来的每一次绘制都生效，所以每个对象各绑一次
            const VkBuffer vertexBuffers[] = {mesh.GetVertexBuffer()};
            const VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

            //绑定索引缓冲。与上面那行有三处不一样：
            //
            //① 没有 binding 编号。索引缓冲全场只有一个槽位，不像顶点缓冲能同时绑好几条
            //   数据流按编号取用 —— 所以这个函数收的是单个句柄，不是数组
            //② 要多给一个索引类型。VkBuffer 只是一段字节，它自己不知道里面装的是 16 位
            //   还是 32 位的索引，这个信息没有别处能推出来，只能在这里说
            //③ 这里那个 0 是缓冲内的**字节偏移**，不是索引下标。要让绘制从第几个索引
            //   开始，那是下面 firstIndex 的事，两者别混
            //
            //索引类型写死成 UINT16 而不做成参数：本项目的索引数组就是 uint16_t，
            //做成参数只是把一个常量串过每一层。代价是它与 DoodleMesh 的 uint16_t 索引绑在一起 ——
            //将来顶点数越过 65535 换成 uint32_t 时，两处要一起改
            vkCmdBindIndexBuffer(commandBuffer, mesh.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

            //按索引绘制。参数从「画几个顶点」变成「按几个索引去取顶点」——
            //顶点数不再需要：索引指向哪个顶点就抓哪个，同一个顶点被两个三角形
            //共用时也只抓一次
            //
            //后三个参数依次是 firstIndex / vertexOffset / firstInstance，这里全 0。
            //它们正是「一块大缓冲里塞很多 mesh」那条路的入口：每个 mesh 各带一组
            //非零值，就不必为它单独建缓冲
            vkCmdDrawIndexed(commandBuffer, mesh.GetIndexCount(), 1, 0, 0, 0);
        }

        //结束渲染
        vkCmdEndRendering(commandBuffer);

        //③ 画完、交回呈现引擎之前：转回呈现布局 —— 原来是渲染通道 finalLayout 的活。
        //为什么这一条的六格与入口那条不对称，写在实现里
        RecordImageTransitionToPresent(commandBuffer, imageIndex);

        //vkCmd* 全部返回 void，这里是整个录制过程唯一的错误检查点
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }

        //交回去的正是刚刚录的那一条。调用方要提交它，而「本帧该用哪一条」
        //只有本层知道 —— 让它成为返回值，就不可能出现「录 A 提交 B」
        return commandBuffer;
    }

    void DoodleCommandBuffer::RecordImageTransitionToColorAttachment(VkCommandBuffer commandBuffer, const uint32_t imageIndex) const
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
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

        //oldLayout 取 UNDEFINED 而不是 PRESENT_SRC_KHR：后者等于声明「上一帧结尾把它转成了
        //呈现布局」，于是录制开始依赖既往状态，首帧与重建后各要一套说法。UNDEFINED 是
        //「旧内容不要了」，反正紧接着就要清屏 —— 每帧从同一条路重算期望态，三条路径收敛
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        //不转移队列族所有权：同族时交换链用 EXCLUSIVE、异族时用 CONCURRENT
        //（见 DoodleSwapChain.cpp），两条路都不需要转移
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_pSwapChain->GetImages()[imageIndex];

        //整张图、单层单级：交换链图像没有 mip 层级，也不是数组
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        //srcAccessMask 取 NONE：上一次碰这张图的是呈现引擎的读，它不在这个内存模型里，
        //access mask 表达不了它 —— 我们要的是执行顺序，不是「谁写过它」的可见性。
        //dstAccessMask 则是被保护的具体操作：接下来对颜色附件的写
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        //两个 stage 都取 COLOR_ATTACHMENT_OUTPUT：src 侧必须与 DrawFrame 里等待信号量的
        //stageMask 一致，转换才会排在 acquire 信号量被等待之后 —— 否则呈现引擎可能还在读
        //这张图，这时动它的布局就是数据竞争；dst 侧是接下来要写它的阶段
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        //sync2 的入口：一批屏障收进一个 VkDependencyInfo。
        //memory 与 buffer 两栏留空 —— 本次只提交图像屏障（旧式里那是 vkCmdPipelineBarrier
        //那一长串参数，现在各自成栏，不用填的自然不用出现）
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleCommandBuffer::RecordImageTransitionToDepthAttachment(
        VkCommandBuffer commandBuffer, const uint32_t frameIndex) const
    {
        //取图之后、开画之前。这一步在渲染通道时代由驱动在通道入口自动完成；
        //动态渲染把它交还给录制者 —— 原文标注为「可以跳过」的那一节，在这里是必做
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

        //每帧都从 UNDEFINED 起算，与本层处理交换链图像那条同一个写法：
        //旧内容反正紧接着就被清屏覆盖，重算期望态比依赖上一帧留下的布局更不容易出错
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        //按 frameIndex 取本帧那一张：本层不缓存上游句柄，重建后重录拿到的自然是新的
        barrier.image = m_pDepthImages->GetImage(frameIndex);

        //切面按格式算：候选表落到带模板的格式时，模板分量必须一起声明
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (DoodleDepthImages::HasStencilComponent(m_pDepthImages->GetFormat()))
        {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        barrier.subresourceRange =
        {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        //源 scope 留空：这张图上一次被用的是同一槽位的上一轮（k-2 帧），
        //而 DrawFrame 开头等本槽位栅栏时，它必定已经执行完了；k-1 帧用的是另一张图。
        //所以这条屏障只负责转布局，不再负责串两帧。NONE 是 sync2 里
        //「源 scope 为空」的正当写法（见《同步的现代写法》）
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;

        //目的地是本帧的深度测试：读（比较）+ 写（通过时写回）。
        //两个 stage 都写，是因为测试落在哪个阶段取决于着色器有没有声明
        //early fragment tests —— 与原文那条 dependency 两个阶段都写同理
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                              | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleCommandBuffer::RecordImageTransitionToPresent(VkCommandBuffer commandBuffer, const uint32_t imageIndex) const
    {
        //画完、交回呈现引擎之前。这一步原来是渲染通道 finalLayout 的活。
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        //队列族与范围同入口那条：不转移所有权、整张图单层单级
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_pSwapChain->GetImages()[imageIndex];
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        //srcAccessMask 用 COLOR_ATTACHMENT_WRITE 把刚画的颜色推到可见：转布局本身要动
        //这份数据，写成 NONE 的话呈现可能读到半成品（撕裂、花屏、偶发的半帧）。
        //这一格与入口那条的 NONE 不对称是刻意的 —— 出口这次源 scope 里真的有写。
        //dstAccessMask 取 NONE：接手的是呈现引擎，它不吃内存可见性这套，只认执行序
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        //dstStageMask 取 NONE：后面没有别的阶段要等 —— 转换只需在提交结束之前完成，
        //而 renderFinished 信号量正是在批次末尾 signal 的，跨引擎那一段由它兜底。
        //
        //旧式里这一格写的是 BOTTOM_OF_PIPE，那是「旧式没有空阶段这个值」时的替身写法；
        //sync2 补上了 NONE，「后面没有任何阶段」终于有了正当写法。
        //这也是整个迁移里唯一一处不是换名字的格
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }
}
