//
//  Singleton.h
//  Doodle
//
//  Created by 郭智 on 2026/9/5.
//
#pragma once

namespace Doodle
{
    /*
     * 单例模板类
     */
    template <typename T>
    class Singleton
    {
    protected:
        Singleton() = default;
        ~Singleton() = default;

    public:
        static T& Instance()
        {
            static T instance;
            return instance;
        }

        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;
    };
}