// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <HAL/Thread.h>

PRAGMA_DISABLE_OVERLOADED_VIRTUAL_WARNINGS

THIRD_PARTY_INCLUDES_START
#include "core/Result.h"
#include "core/Factory.h"
#include "components/VideoEncoderVCE.h"
#include "core/Compute.h"
#include "core/Plane.h"
THIRD_PARTY_INCLUDES_END

PRAGMA_ENABLE_OVERLOADED_VIRTUAL_WARNINGS

namespace AVEncoder
{
    class FAmfCommon
    {
    public:
        // attempt to load Amf
        static FAmfCommon &Setup();

        // shutdown - release loaded dll
        static void Shutdown();

        bool GetIsAvailable() const { return bIsAvailable; }

    private:
        FAmfCommon() = default;

		void SetupAmfFunctions();

        static FCriticalSection ProtectSingleton;
        static FAmfCommon Singleton;

        amf_handle DllHandle = nullptr;
        amf::AMFFactory *AmfFactory = nullptr;
        amf::AMFContextPtr AmfContext;

        bool bIsAvailable = false;
        bool bWasSetUp = false;
    };
}