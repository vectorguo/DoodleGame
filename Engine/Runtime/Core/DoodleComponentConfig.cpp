//
//  DoodleComponentConfig.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleComponentConfig.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace Doodle
{
    DoodleComponentConfig::DoodleComponentConfig(nlohmann::json settings, std::string path)
        : m_settings(std::move(settings))
        , m_path(std::move(path))
    {
        if (!m_settings.is_object())
        {
            throw std::runtime_error(
                (m_path.empty() ? std::string("<component settings>") : m_path) +
                ": expected an object");
        }
    }

    bool DoodleComponentConfig::GetBool(const std::string_view key, const bool defaultValue) const
    {
        const nlohmann::json* const pValue = FindValue(key);
        if (pValue == nullptr)
        {
            return defaultValue;
        }

        if (!pValue->is_boolean())
        {
            throw std::runtime_error(KeyPath(key) + ": expected a boolean");
        }

        return pValue->get<bool>();
    }

    int DoodleComponentConfig::GetInt(const std::string_view key, const int defaultValue) const
    {
        const nlohmann::json* const pValue = FindValue(key);
        if (pValue == nullptr)
        {
            return defaultValue;
        }

        int64_t number = 0;
        if (pValue->is_number_unsigned())
        {
            const uint64_t unsignedNumber = pValue->get<uint64_t>();
            if (unsignedNumber > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            {
                throw std::runtime_error(KeyPath(key) + ": is out of range for int");
            }
            number = static_cast<int64_t>(unsignedNumber);
        }
        else if (pValue->is_number_integer())
        {
            number = pValue->get<int64_t>();
        }
        else
        {
            throw std::runtime_error(KeyPath(key) + ": expected an integer");
        }

        if (number < std::numeric_limits<int>::min() ||
            number > std::numeric_limits<int>::max())
        {
            throw std::runtime_error(KeyPath(key) + ": is out of range for int");
        }

        return static_cast<int>(number);
    }

    float DoodleComponentConfig::GetFloat(const std::string_view key, const float defaultValue) const
    {
        const nlohmann::json* const pValue = FindValue(key);
        if (pValue == nullptr)
        {
            return defaultValue;
        }

        return ToFiniteFloat(*pValue, KeyPath(key));
    }

    std::string DoodleComponentConfig::GetString(const std::string_view key, std::string defaultValue) const
    {
        const nlohmann::json* const pValue = FindValue(key);
        if (pValue == nullptr)
        {
            return defaultValue;
        }

        if (!pValue->is_string())
        {
            throw std::runtime_error(KeyPath(key) + ": expected a string");
        }

        //空串是合法的：可选引用用空串表示「没有目标」
        return pValue->get<std::string>();
    }

    glm::vec3 DoodleComponentConfig::GetVec3(const std::string_view key, const glm::vec3& defaultValue) const
    {
        const nlohmann::json* const pValue = FindValue(key);
        if (pValue == nullptr)
        {
            return defaultValue;
        }

        if (!pValue->is_array() || pValue->size() != 3)
        {
            throw std::runtime_error(KeyPath(key) + ": expected an array of 3 numbers");
        }

        glm::vec3 result{0.0f};
        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::string elementPath = KeyPath(key) + "[" + std::to_string(i) + "]";
            result[i] = ToFiniteFloat((*pValue)[i], elementPath);
        }

        return result;
    }

    void DoodleComponentConfig::SetBool(const std::string_view key, const bool value)
    {
        m_settings[std::string(key)] = value;
    }

    void DoodleComponentConfig::SetInt(const std::string_view key, const int value)
    {
        m_settings[std::string(key)] = value;
    }

    void DoodleComponentConfig::SetFloat(const std::string_view key, const float value)
    {
        m_settings[std::string(key)] = value;
    }

    void DoodleComponentConfig::SetString(const std::string_view key, const std::string_view value)
    {
        m_settings[std::string(key)] = std::string(value);
    }

    void DoodleComponentConfig::SetVec3(const std::string_view key, const glm::vec3& value)
    {
        m_settings[std::string(key)] = nlohmann::json::array({value.x, value.y, value.z});
    }

    bool DoodleComponentConfig::Has(const std::string_view key) const
    {
        return FindValue(key) != nullptr;
    }

    void DoodleComponentConfig::ExpectOnlyKeys(const std::initializer_list<std::string_view> allowedKeys) const
    {
        for (auto it = m_settings.begin(); it != m_settings.end(); ++it)
        {
            const std::string_view key(it.key());

            bool known = false;
            for (const std::string_view allowed : allowedKeys)
            {
                if (key == allowed)
                {
                    known = true;
                    break;
                }
            }

            if (!known)
            {
                throw std::runtime_error(KeyPath(key) + ": unknown field '" + std::string(key) + "'");
            }
        }
    }

    const nlohmann::json* DoodleComponentConfig::FindValue(const std::string_view key) const
    {
        const auto it = m_settings.find(std::string(key));
        if (it == m_settings.end())
        {
            return nullptr;
        }

        return &(*it);
    }

    std::string DoodleComponentConfig::KeyPath(const std::string_view key) const
    {
        if (m_path.empty())
        {
            return std::string(key);
        }

        return m_path + "." + std::string(key);
    }

    float DoodleComponentConfig::ToFiniteFloat(const nlohmann::json& value, const std::string& path)
    {
        if (!value.is_number())
        {
            throw std::runtime_error(path + ": expected a number");
        }

        const double number = value.get<double>();
        if (!std::isfinite(number))
        {
            throw std::runtime_error(path + ": must be a finite number");
        }

        const auto converted = static_cast<float>(number);
        if (!std::isfinite(converted))
        {
            throw std::runtime_error(path + ": is out of range for float");
        }

        return converted;
    }
}
