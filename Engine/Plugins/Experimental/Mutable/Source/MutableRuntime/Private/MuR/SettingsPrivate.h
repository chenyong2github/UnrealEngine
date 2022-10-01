// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Settings.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Private Settings implementation
    //---------------------------------------------------------------------------------------------
    class Settings::Private : public Base
    {
    public:
        bool m_profile = false;
        uint64_t m_streamingCacheBytes = 0;
        int m_imageCompressionQuality = 0;
    };


}
