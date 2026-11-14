// 方向光 + 环境项，输出线性空间的颜色。
//
// 所有片元着色器共用这一套光照：环境项兜住背光面，漫反射的上限吃掉环境项，
// 白光照满时正好回到基色，不会因为两项相加超过基色。
//
// 调用前先把世界空间法线归一化好；双面材质（树叶）在调用方按 gl_FrontFacing
// 翻转法线，光照本身不关心面朝哪边。
//
// wrap 把明暗交界线往背光面推：0 是标准 Lambert（硬一点的交界），
// 大于 0 时背光侧也有少量光照，交界线更柔和 —— 树皮这类有机材质用得上

vec3 EvaluateLighting(in vec3 worldNormal, in vec3 albedo, in float wrap)
{
    // 环境项：背光面也不至于全黑。它由常量给出，不受光源颜色影响 ——
    // 场景里没有光源时（lightColor 为 0），画面就只剩这一项
    const float ambient = 0.22;

    const vec3 normal = normalize(worldNormal);

    // lightDirection 是光线传播方向（从光源指向场景），
    // 朝向光源的那一侧是它的反方向；点乘小于 0 的部分是背光面
    const float rawDiffuse = dot(normal, -ubo.lightDirection.xyz);
    const float diffuse = max(rawDiffuse + wrap, 0.0) / (1.0 + wrap);

    return albedo * (ambient + (1.0 - ambient) * diffuse * ubo.lightColor.rgb);
}
