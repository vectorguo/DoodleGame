//
//  DoodleMaterialTextureSlot.h
//  Doodle
//

#pragma once

#include "Utility/DoodleCommon.h"

namespace Doodle
{
    /**
     * 材质对着色器提出的契约：set 1 的规则
     *
     * 与 DoodleRenderPassLayout 是一对：那份按 pass 规定每帧常量与 push constant，
     * 这份规定 set 1 —— 材质贴图。set 1 的形状不归 pass 管，也不写死成某几张贴图：
     * 每个着色器在 GLSL 里声明自己要几张，DoodleShader 加载时反射出来、按下面的
     * 规则核对，再建这个着色器自己的 set 1 布局。
     *
     * 规则：
     * - set 1 只放贴图：每个绑定是一张 combined image sampler（sampler2D），
     *   数量为 1；采样器随贴图一起写入，着色器里不另声明；
     * - 每个绑定必须有变量名：材质按变量名配贴图（"textures": { "albedoTexture": ... }）；
     * - 最多 kMaxMaterialTextureSlots 个；
     * - 同一个着色器的各个 pass 合用一份 set 1：同一个 binding 在各 pass 里必须同名、
     *   同类型，同一个名字不能占两个 binding。布局按所有 pass 合并、阶段取并集，
     *   所以一个材质只要一个 set 1，forward、shadow 都绑它。
     *
     * 只有着色器真正读到的贴图才算槽：反射按入口点取绑定，声明了却没用的贴图不会出现。
     *
     * 两处读它：DoodleShader 加载时按它核对并建 set 1 布局；DoodleDescriptorManager
     * 按着色器反射出的贴图槽给每个材质分配、写入 set 1。
     */

    /**
     * 一个着色器最多声明几张材质贴图
     *
     * 规范只保证每个阶段至少能用 16 个采样器（maxPerStageDescriptorSamplers），
     * 余量留给以后和每帧常量放在同一个集合里的 pass 级贴图（例如 shadow map）。DoodleDescriptorManager
     * 也按它给描述符池定容量：任何一个材质集合都放得进一块新池
     */
    inline constexpr uint32_t kMaxMaterialTextureSlots = 8;

    /**
     * 一个贴图槽：着色器 set 1 里的一个绑定
     *
     * 由 DoodleShader 反射得到，按 binding 升序排列。材质按 name 配贴图，
     * 描述符管理器按 binding 写入
     */
    struct DoodleMaterialTextureSlot
    {
        /**
         * set 1 里的绑定号
         */
        uint32_t binding = 0;

        /**
         * GLSL 里的变量名，如 "albedoTexture"；材质文件按它配贴图
         */
        std::string name;
    };
}
