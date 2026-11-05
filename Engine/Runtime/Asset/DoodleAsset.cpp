//
//  DoodleAsset.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/1.
//

#include "DoodleAsset.h"

#include <stdexcept>

namespace Doodle
{
    DoodleAsset::DoodleAsset(const std::string_view assetPath)
        : m_assetPath(assetPath)
    {
        if (m_assetPath.empty())
        {
            throw std::invalid_argument("DoodleAsset: assetPath must not be empty");
        }
    }

    DoodleAsset::~DoodleAsset() = default;
}
