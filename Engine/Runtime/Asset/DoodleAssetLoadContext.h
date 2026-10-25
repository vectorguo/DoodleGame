//
//  DoodleAssetLoadContext.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

namespace Doodle
{
    class DoodleAssetManager;
    class DoodleFileSystem;

    /**
     * 一次加载拿得到的共享服务
     *
     * 它是 loader 与系统之间唯一的数据通道。loader 自己构造时拿到的依赖
     * （设备之类）留在 loader 内部；这里放的是「每次加载都需要问系统」的东西：
     * 文件来源，以及将来材质加载贴图这类依赖时用的资产管理器本身。
     *
     * 只在 Load 调用期间有效，loader 不得保存它的引用。
     */
    struct DoodleAssetLoadContext
    {
        /**
         * 文件来源；由应用持有，寿命覆盖整个资产管理器
         */
        const DoodleFileSystem& fileSystem;

        /**
         * 资产管理器自身，供 loader 递归加载依赖
         *
         * 现在没有使用者；先放进来，是为了将来材质 loader 调
         * context.assetManager.Load<DoodleTexture>(...) 时不用改接口。
         * 循环依赖检测留到真的出现依赖链时再做
         */
        DoodleAssetManager& assetManager;
    };
}
