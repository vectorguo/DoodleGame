//
//  DoodleAssetManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleAssetManager.h"

#include "DoodleMeshLoader.h"
#include "DoodleSceneLoader.h"

#include <stdexcept>
#include <utility>

namespace Doodle
{
    DoodleAssetManager* DoodleAssetManager::s_pInstance = nullptr;

    DoodleAssetManager::DoodleAssetManager()
    {
        // 登记在构造里：资产管理器是应用的成员，登记之后到析构之前，
        // GetInstance 拿到的都是这一份。
        s_pInstance = this;
    }

    DoodleAssetManager::~DoodleAssetManager()
    {
        // 先撤登记：析构一旦开始，这个对象就不再是「当前资产管理器」。
        // 认一下是不是自己，和 DoodleSceneManager 的析构同理。
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }

        // 不在析构里销毁 GPU 资产：正常路径由 DoodleApplication 先 WaitIdle、
        // 再显式 Destroy；异常退出时可能还有在途命令，此时碰 Vulkan 比把资源
        // 交给进程回收更危险。loader 对象本身由容器析构释放，它们的析构器
        // 因此不得释放设备资源 —— 那是 Destroy 的职责
    }

    void DoodleAssetManager::Initialize(const DoodleAssetManagerInitContext& context)
    {
        if (m_pFileSystem != nullptr)
        {
            throw std::logic_error("DoodleAssetManager: Initialize called twice");
        }

        m_pFileSystem = &context.fileSystem;

        try
        {
            // 网格 loader 只记下设备；内建正方体在第一次 Load 时才上传。
            // 场景 loader 是纯 CPU 的，只需要通过上下文拿文件来源
            RegisterLoader<DoodleMesh>(std::make_unique<DoodleMeshLoader>(context.device));
            RegisterLoader<DoodleSceneData>(std::make_unique<DoodleSceneLoader>());
        }
        catch (...)
        {
            // 半途失败不留半套 loader：已经注册的那些可能是 GPU 资源持有者
            Destroy();
            throw;
        }
    }

    void DoodleAssetManager::Destroy()
    {
        // 先让每个 loader 释放资产，再清容器。loader 的 Destroy 必须不抛异常：
        // 它只在显式销毁路径上调用（析构不做 GPU 清理）
        for (auto& entry : m_loaders)
        {
            entry.second->Destroy();
        }

        m_loaders.clear();
        m_pFileSystem = nullptr;
    }
}
