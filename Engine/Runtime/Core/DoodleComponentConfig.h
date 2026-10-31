//
//  DoodleComponentConfig.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Doodle
{
    /**
     * 一个组件的序列化 settings
     *
     * 这是组件的「序列化载体」，同一份数据有两个方向：
     *   - Deserialize(const DoodleComponentConfig&)：只读，组件把配置写进自己的成员；
     *   - Serialize(DoodleComponentConfig&) const：写入，组件把自己的成员写回配置。
     *
     * const 就是方向开关：反序列化拿 const&，编译期就写不进去；序列化拿非 const&。
     *
     * 当前由 DoodleSceneLoader 解析场景文件时构造；序列化写回以后也会构造一个空对象来填。
     * 游戏运行时不应该持有它 —— 组件应当在 Deserialize / OnAwake 里把值复制到成员，
     * 之后只用自己的成员。
     *
     * 配置本身不知道自己来自哪个场景 / 对象：读接口的错误只带字段名，例如
     * "moveSpeed: expected a number"。需要文件与对象前缀时，由装配层
     * （DoodleScene / DoodleSceneImporter）在异常外面补。
     */
    class DoodleComponentConfig final
    {
    public:
        /**
         * 空的 settings，供序列化写回使用
         */
        DoodleComponentConfig() = default;

        /**
         * 由 loader / 场景写入器构造
         *
         * @param settings 必须是一个 JSON object
         */
        DoodleComponentConfig(nlohmann::json settings);

        /**
         * 底层 JSON
         *
         * 只给未来的场景写入器和测试用；组件逻辑请走类型化 getter
         */
        [[nodiscard]] const nlohmann::json& GetJson() const
        {
            return m_settings;
        }

        /**
         * 读布尔值
         *
         * key 不存在时返回 defaultValue；存在但不是布尔则抛错
         */
        [[nodiscard]] bool GetBool(std::string_view key, bool defaultValue) const;

        /**
         * 写入 / 覆盖一个布尔字段
         */
        void SetBool(std::string_view key, bool value);

        /**
         * 读整数
         *
         * key 不存在时返回 defaultValue；接受 JSON 的有符号 / 无符号整数，
         * 超出 int 范围或不是整数则抛错
         */
        [[nodiscard]] int GetInt(std::string_view key, int defaultValue) const;

        /**
         * 写入 / 覆盖一个整数字段
         */
        void SetInt(std::string_view key, int value);

        /**
         * 读浮点数
         *
         * key 不存在时返回 defaultValue；必须是有限数，double 转 float 溢出则抛错
         */
        [[nodiscard]] float GetFloat(std::string_view key, float defaultValue) const;

        /**
         * 写入 / 覆盖一个浮点字段
         *
         * 不做范围校验：写进来的是组件当前成员值，序列化不负责修正状态
         */
        void SetFloat(std::string_view key, float value);

        /**
         * 读字符串
         *
         * key 不存在时返回 defaultValue；存在时允许空串（空串常用来表示“没有引用”），
         * 不是字符串则抛错
         */
        [[nodiscard]] std::string GetString(std::string_view key, std::string defaultValue) const;

        /**
         * 写入 / 覆盖一个字符串字段；允许空串
         */
        void SetString(std::string_view key, std::string_view value);

        /**
         * 读三维向量
         *
         * key 不存在时返回 defaultValue；必须是由 3 个有限数字组成的数组
         */
        [[nodiscard]] glm::vec3 GetVec3(std::string_view key, const glm::vec3& defaultValue) const;

        /**
         * 写入 / 覆盖一个三维向量字段，写成 [x, y, z] 数组
         */
        void SetVec3(std::string_view key, const glm::vec3& value);

        /**
         * 读四维向量
         *
         * key 不存在时返回 defaultValue；必须是由 4 个有限数字组成的数组。
         * 材质基色这类要进 push constant 的 vec4 走这个入口
         */
        [[nodiscard]] glm::vec4 GetVec4(std::string_view key, const glm::vec4& defaultValue) const;

        /**
         * 写入 / 覆盖一个四维向量字段，写成 [x, y, z, w] 数组
         */
        void SetVec4(std::string_view key, const glm::vec4& value);

        /**
         * 读「对象数组」
         *
         * key 不存在时返回空数组；存在时必须是数组，且每个元素是 object。
         * 每个元素包成一个子 DoodleComponentConfig；数组形状的错误带
         * "key[i]" 前缀，子配置内部的字段错误只带字段名，需要元素下标时
         * 由调用方自己补（例如 DoodleMeshRenderer 的 materials）。
         *
         * 用途是「一个组件下挂一组配置对象」的字段（例如 renderer 的材质表）。
         * 返回的是拷贝：对象数量很小，换调用方随手存放的简单性
         */
        [[nodiscard]] std::vector<DoodleComponentConfig> GetObjectArray(std::string_view key) const;

        /**
         * 写入 / 覆盖一个对象数组
         *
         * 每个子配置的根必须是一个 object；空数组写成 []
         */
        void SetObjectArray(std::string_view key, const std::vector<DoodleComponentConfig>& values);

        /**
         * key 是否存在于 settings 根层
         *
         * 只判断存在性，不检查类型，也不递归子对象
         */
        [[nodiscard]] bool Has(std::string_view key) const;

        /**
         * 拒绝未声明的字段
         *
         * 组件的 Deserialize 应当调用一次。没有反射，引擎不知道组件有哪些字段，
         * 这是唯一能抓住 "moveSped" 这类拼写错误的地方。
         *
         * 本函数本身总是检查；组件在 #ifndef NDEBUG 里调用它，所以 Release
         * 不做字段白名单检查。开关放在调用点，而不是函数内部。
         */
        void ExpectOnlyKeys(std::initializer_list<std::string_view> allowedKeys) const;

    private:
        /**
         * 按 key 查根层的 JSON 值；不存在返回 nullptr
         */
        [[nodiscard]] const nlohmann::json* FindValue(std::string_view key) const;

        /**
         * JSON 数字 → 有限 float；类型、有限性、float 溢出错误都带字段名抛出
         */
        [[nodiscard]] static float ToFiniteFloat(const nlohmann::json& value, const std::string& fieldName);

    private:
        /**
         * 组件参数，始终是一个 JSON object
         */
        nlohmann::json m_settings = nlohmann::json::object();
    };
}
