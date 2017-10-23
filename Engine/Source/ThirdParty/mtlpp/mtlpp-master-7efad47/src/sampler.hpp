/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/mtlpp/blob/master/LICENSE
 */
// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
// Modifications for Unreal Engine

#pragma once

#include "defines.hpp"
#include "depth_stencil.hpp"
#include "device.hpp"

MTLPP_CLASS(MTLSamplerDescriptor);
MTLPP_PROTOCOL(MTLSamplerState);

namespace mtlpp
{
    enum class SamplerMinMagFilter
    {
        Nearest = 0,
        Linear  = 1,
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    enum class SamplerMipFilter
    {
        NotMipmapped = 0,
        Nearest      = 1,
        Linear       = 2,
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    enum class SamplerAddressMode
    {
        ClampToEdge                                   = 0,
        MirrorClampToEdge  MTLPP_AVAILABLE_MAC(10_11) = 1,
        Repeat                                        = 2,
        MirrorRepeat                                  = 3,
        ClampToZero                                   = 4,
        ClampToBorderColor MTLPP_AVAILABLE_MAC(10_12) = 5,
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    enum class SamplerBorderColor
    {
        TransparentBlack = 0,  // {0,0,0,0}
        OpaqueBlack = 1,       // {0,0,0,1}
        OpaqueWhite = 2,       // {1,1,1,1}
    };

    class SamplerDescriptor : public ns::Object<MTLSamplerDescriptor*>
    {
    public:
        SamplerDescriptor();
        SamplerDescriptor(MTLSamplerDescriptor* handle) : ns::Object<MTLSamplerDescriptor*>(handle) { }

        SamplerMinMagFilter GetMinFilter() const;
        SamplerMinMagFilter GetMagFilter() const;
        SamplerMipFilter    GetMipFilter() const;
        uint32_t            GetMaxAnisotropy() const;
        SamplerAddressMode  GetSAddressMode() const;
        SamplerAddressMode  GetTAddressMode() const;
        SamplerAddressMode  GetRAddressMode() const;
        SamplerBorderColor  GetBorderColor() const MTLPP_AVAILABLE_MAC(10_12);
        bool                IsNormalizedCoordinates() const;
        float               GetLodMinClamp() const;
        float               GetLodMaxClamp() const;
        CompareFunction     GetCompareFunction() const MTLPP_AVAILABLE(10_11, 9_0);
        ns::String          GetLabel() const;
		
		bool SupportArgumentBuffers() const MTLPP_AVAILABLE(10_13, 11_0);

        void SetMinFilter(SamplerMinMagFilter minFilter);
        void SetMagFilter(SamplerMinMagFilter magFilter);
        void SetMipFilter(SamplerMipFilter mipFilter);
        void SetMaxAnisotropy(uint32_t maxAnisotropy);
        void SetSAddressMode(SamplerAddressMode sAddressMode);
        void SetTAddressMode(SamplerAddressMode tAddressMode);
        void SetRAddressMode(SamplerAddressMode rAddressMode);
        void SetBorderColor(SamplerBorderColor borderColor) MTLPP_AVAILABLE_MAC(10_12);
        void SetNormalizedCoordinates(bool normalizedCoordinates);
        void SetLodMinClamp(float lodMinClamp);
        void SetLodMaxClamp(float lodMaxClamp);
        void SetCompareFunction(CompareFunction compareFunction) MTLPP_AVAILABLE(10_11, 9_0);
        void SetLabel(const ns::String& label);
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    class SamplerState : public ns::Object<ns::Protocol<id<MTLSamplerState>>::type>
    {
    public:
        SamplerState() { }
        SamplerState(ns::Protocol<id<MTLSamplerState>>::type handle) : ns::Object<ns::Protocol<id<MTLSamplerState>>::type>(handle) { }

        ns::String GetLabel() const;
        Device     GetDevice() const;
    }
    MTLPP_AVAILABLE(10_11, 8_0);
}

