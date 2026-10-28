//
//  DoodleComponentRegistry.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleComponentRegistry.h"

#include <stdexcept>
#include <utility>

namespace Doodle
{
    DoodleComponentRegistry& DoodleComponentRegistry::GetInstance()
    {
        //函数内 static：首次使用时构造，进程结束时销毁。
        //这样不依赖翻译单元之间的静态初始化顺序，也不需要显式 Initialize
        static DoodleComponentRegistry registry;
        return registry;
    }

    void DoodleComponentRegistry::Register(const DoodleComponentRegistration& registration)
    {
        //空 name 无法成为场景里的 key；空 create 意味着登记了却建不出来。
        //两者都是程序错误，启动时直接报，别拖到内容加载
        if (registration.name.empty())
        {
            throw std::invalid_argument(
                "DoodleComponentRegistry: component type name must not be empty");
        }

        if (registration.create == nullptr)
        {
            throw std::invalid_argument(
                "DoodleComponentRegistry: component '" + std::string(registration.name) +
                "' has no create function");
        }

        //先查重：重复登记不应该覆盖已有项
        if (m_registrations.find(registration.name) != m_registrations.end())
        {
            throw std::logic_error("DoodleComponentRegistry: duplicate component type '" +
                                   std::string(registration.name) + "'");
        }

        //map 的 key 自己拷贝一份 name；注册项里的 string_view 仍指向调用方传入的字符串，
        //所以 RegisterComponent 要求传字面量 / 静态字符串
        const bool inserted =
            m_registrations.emplace(std::string(registration.name), registration).second;

        //查重已经挡过；这里只防不可能的单线程竞态，保持错误语义一致
        if (!inserted)
        {
            throw std::logic_error("DoodleComponentRegistry: duplicate component type '" +
                                   std::string(registration.name) + "'");
        }
    }

    const DoodleComponentRegistration* DoodleComponentRegistry::Find(
        const std::string_view name) const
    {
        //std::less<> 的透明比较让 string_view 直接查 map<std::string>，
        //不需要为一次查找临时构造 std::string
        const auto it = m_registrations.find(name);
        if (it == m_registrations.end())
        {
            return nullptr;
        }

        //返回 map 节点地址：注册表不删除节点，指针在本进程内保持有效
        return &it->second;
    }
}
