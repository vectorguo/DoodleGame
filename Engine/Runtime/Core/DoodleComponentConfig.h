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
     * 读接口一律带路径报错，例如：
     *   Scenes/main.json: gameObjects[2].components[0].settings.moveSpeed:
     *       expected a number
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
         * @param path 只用于错误消息，例如 "Scenes/main.json: gameObjects[2].components[0].settings"
         */
        DoodleComponentConfig(nlohmann::json settings, std::string path);

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
         * 本配置的来源路径，组件自己报语义错误时可以使用
         */
        [[nodiscard]] const std::string& GetPath() const
        {
            return m_path;
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
         * key 是否存在于 settings 根层
         *
         * 只判断存在性，不检查类型；当前 settings 是平的，没有嵌套字段
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
         * 拼出 "来源路径.key" 形式的错误前缀；路径为空时只返回 key
         */
        [[nodiscard]] std::string KeyPath(std::string_view key) const;

        /**
         * JSON 数字 → 有限 float；类型、有限性、float 溢出错误都带路径抛出
         */
        [[nodiscard]] static float ToFiniteFloat(const nlohmann::json& value, const std::string& path);

    private:
        /**
         * 组件参数，始终是一个 JSON object
         */
        nlohmann::json m_settings = nlohmann::json::object();

        /**
         * 错误消息用的来源路径；空表示没有来源（测试 / 序列化）
         */
        std::string m_path;
    };
}
