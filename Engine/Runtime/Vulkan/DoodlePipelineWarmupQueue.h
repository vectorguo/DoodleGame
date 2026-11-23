//
//  DoodlePipelineWarmupQueue.h
//  Doodle
//
//  Created by 郭智 on 2026/11/14.
//

#pragma once

#include <mutex>
#include <vector>

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleShader;
    class DoodleRenderPipeline;

    /**
     * 待预热队列：着色器资产 → 渲染管线的单向邮箱
     *
     * **解决什么**：每个着色器在一个 pass 里只有一条管线，与绘制项数量无关。
     * 按绘制项每帧扫描去发现新着色器，成本会随世界规模线性增长，还要再乘
     * pass 数；本队列把触发点挪到「着色器初始化成功」这一刻，稳态每帧只做一次判空。
     *
     * **谁生产**：DoodleShader::Initialize 成功之后投递自己。那是着色器可用的唯一
     * 状态转换点，投递跟着它走，不依赖加载器记得通知。
     *
     * **谁消费**：DoodleRenderPipeline::WarmupPipelines 在取帧之前排空，按 pass 分发。
     * 每个 pass 自己决定把着色器映射成哪份管线描述。
     *
     * **为什么独立成类**：DoodleShader（Rendering）与 DoodleRenderPipeline（Vulkan）
     * 之间不能互相依赖；两边共同依赖这个只装指针的邮箱，依赖方向不变。
     *
     * **生命周期**：由 DoodleRenderPipeline 独占持有，随渲染管线构造时登记、析构时清空。
     * 队列里的指针在 DoodleAssetManager::Destroy 之前有效：着色器只在 Initialize 成功
     * 之后投递，而 DoodleAssetLoader::Load 在 Create 之前就占好了缓存位置，投进来的
     * 着色器一定留在缓存里。运行时拆解时由
     * DoodleRenderPipeline::Destroy 开头清空 —— 资产先于渲染管线销毁的那段窗口里
     * 没有任何排空路径，指针不会被解引用，Android 拆掉再起来也不会留下上一轮的残留。
     *
     * 不可复制：复制一份就是两个对象各自持有同一批待预热项
     */
    class DoodlePipelineWarmupQueue
    {
        friend class DoodleRenderPipeline;

    private:
        /**
         * 只能由 DoodleRenderPipeline 创建：队列随渲染管线只有一份，
         * 实例由管线独占持有
         */
        DoodlePipelineWarmupQueue()
        {
            s_pInstance = this;
        }

    public:
        ~DoodlePipelineWarmupQueue()
        {
            if (s_pInstance == this)
            {
                s_pInstance = nullptr;
            }
        }

        DoodlePipelineWarmupQueue(const DoodlePipelineWarmupQueue&) = delete;
        DoodlePipelineWarmupQueue& operator=(const DoodlePipelineWarmupQueue&) = delete;

        /**
         * 当前那一份队列
         *
         * 有效期就是队列对象的存活期：随渲染管线构造时登记、析构时清空。
         * 进程里没有队列时（构造之前、析构之后）取用是未定义行为，与
         * DoodleAssetManager / DoodleSceneManager 的口径一致。
         * 返回引用而不是指针：队列随渲染管线对象一起存在，渲染管线活着就意味着它已经登记
         */
        [[nodiscard]] static DoodlePipelineWarmupQueue& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 投递一个刚刚初始化成功的着色器；任意线程可调
         *
         * 不在这里建管线：建管线需要一个 pass 的管线表与设备，属于渲染侧。
         * 重复投递是安全的：管线表按着色器命中，第二次只是一次查表
         */
        void Push(const DoodleShader& shader);

        /**
         * 取走当前全部待预热项并清空；由渲染线程在取帧之前调用
         *
         * 稳态返回空：此时每帧的开销只有一次加锁与一次判空
         */
        [[nodiscard]] std::vector<const DoodleShader*> Drain();

        /**
         * 丢掉全部待预热项，不建任何管线
         *
         * 运行时拆解时调用：资产先于队列销毁的话，留着这些指针就没有意义了
         */
        void Clear();

    private:
        /**
         * 当前实例，构造时登记、析构时清空
         */
        static DoodlePipelineWarmupQueue* s_pInstance;

        /**
         * 保护 m_pending
         */
        std::mutex m_mutex;

        /**
         * 待建管线的着色器
         *
         * 非拥有指针：着色器资产常驻到资产管理器销毁，地址稳定
         */
        std::vector<const DoodleShader*> m_pending;
    };
}
