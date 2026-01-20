/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#define _GLIBCXX_USE_CXX11_ABI 0

#include <memory>
#include <cstring>

#include <starfish-media-pipeline/StarfishMediaAPIs.h>
#include "MediaPipelineWebOS.h"

std::unique_ptr<char[]> CMediaPipelineWebOS::FeedLegacy(StarfishMediaAPIs* api,
                                                        const char* payload)
{
    // Guard against null API or payload
    if (!api || !payload)
        return {};

    // Call Feed, which returns a std::string
    auto res = api->Feed(payload);

    // Allocate buffer with space for null terminator
    auto p = std::make_unique<char[]>(res.size() + 1);

    // Copy contents
    std::memcpy(p.get(), res.c_str(), res.size() + 1); // includes '\0'

    return p;
}

/*bool CMediaPipelineWebOS::getLegacyMaxVideoResolution(const char* codec, int32_t* maxWidth,
    int32_t* maxHeight, int32_t* maxFramerate)
{
    std::string legacyCodec(codec);
    return smp::util::getMaxVideoResolution(legacyCodec, maxWidth, maxHeight, maxFramerate);
}*/
