// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <HAL/Thread.h>

#if PLATFORM_DESKTOP && !PLATFORM_APPLE

THIRD_PARTY_INCLUDES_START

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#endif

#include "core/Factory.h"
#include "core/Interface.h"
#include "components/VideoEncoderVCE.h"

#if PLATFORM_WINDOWS
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_END

#define CHECK_AMF_RET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (!(Res== AMF_OK || Res==AMF_ALREADY_INITIALIZED))\
	{\
		UE_LOG(LogAVEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
		return;\
	}\
}

namespace AVEncoder
{
	using namespace amf;

    class FAmfCommon
    {
    public:
        // attempt to load Amf
        static FAmfCommon &Setup();

        // shutdown - release loaded dll
        static void Shutdown();

        bool GetIsAvailable() const { return bIsAvailable; }

		bool GetIsCtxInitialized() const { return bIsCtxInitialized; }
		
		bool CreateEncoder(amf::AMFComponentPtr& outEncoder);

		AMFContextPtr GetContext() { return AmfContext; }

		bool bIsCtxInitialized = false;
    private:
        FAmfCommon() = default;

		void SetupAmfFunctions();

        static FCriticalSection ProtectSingleton;
        static FAmfCommon Singleton;

        amf_handle DllHandle = nullptr;
        AMFFactory *AmfFactory = nullptr;
		AMFContextPtr AmfContext;
        bool bIsAvailable = false;
        bool bWasSetUp = false;
    };
}

#endif // PLATFORM_DESKTOP && !PLATFORM_APPLE