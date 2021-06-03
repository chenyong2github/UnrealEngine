// Copyright Epic Games, Inc. All Rights Reserved.

#include "Amf_Common.h"
#include "HAL/Platform.h"

#include "AVEncoder.h"

#include "RHI.h"

namespace AVEncoder
{
    FCriticalSection    FAmfCommon::ProtectSingleton;
    FAmfCommon          FAmfCommon::Singleton;

    // attempt to load Amf
    FAmfCommon &FAmfCommon::Setup()
    {
        FScopeLock Guard(&ProtectSingleton);
        if (!Singleton.bWasSetUp)
        {
            Singleton.bWasSetUp = true;
            Singleton.SetupAmfFunctions();
        }
        return Singleton;
    }

    // shutdown - release loaded dll
    void FAmfCommon::Shutdown()
    {
        FScopeLock Guard(&ProtectSingleton);
        if (Singleton.bWasSetUp)
        {
            Singleton.bWasSetUp = false;
            Singleton.bIsAvailable = false;
            
            if (Singleton.AmfContext)
            {
                Singleton.AmfContext->Terminate();
                Singleton.AmfContext = nullptr;
            }
            
            Singleton.AmfFactory = nullptr;
            
            if (Singleton.DllHandle)
            {
                FPlatformProcess::FreeDllHandle(Singleton.DllHandle);
                Singleton.DllHandle = nullptr;
            }
        }
    }

    bool FAmfCommon::CreateEncoder(amf::AMFComponentPtr& outEncoder)
    {
		AMF_RESULT res = AmfFactory->CreateComponent(AmfContext, AMFVideoEncoderVCE_AVC, &outEncoder);
		if (res != AMF_OK)
		{
			UE_LOG(LogAVEncoder, Error, TEXT("AMF failed to create Encoder component with code: %d"), res);
			return false;
		}
        return true;
    }

    void FAmfCommon::SetupAmfFunctions()
    {
	    check(!bIsAvailable);

		// Can't use Amf without and AMD GPU (also no point if its not the one RHI is using)
		if (!IsRHIDeviceAMD())
		{
			return;
		}

#ifdef AMF_DLL_NAMEA
        DllHandle = FPlatformProcess::GetDllHandle(TEXT(AMF_DLL_NAMEA));

        AMFInit_Fn AmfInitFn = (AMFInit_Fn)FPlatformProcess::GetDllExport(DllHandle, TEXT(AMF_INIT_FUNCTION_NAME));
#else
        AMFInit_Fn AmfInitFn = nullptr;
#endif
        if (AmfInitFn == nullptr)
        {
            return;
        }

        CHECK_AMF_RET(AmfInitFn(AMF_FULL_VERSION, &AmfFactory));

        CHECK_AMF_RET(AmfFactory->CreateContext(&AmfContext));

		// TODO this needs to get moved to lazy initialize when the encoder is actually called 
		{
			FString RHIName = GDynamicRHI->GetName();

			if (RHIName == "D3D11")
			{
				AmfContext->InitDX11(GDynamicRHI->RHIGetNativeDevice());
			}
			else if (RHIName == "D3D12")
			{
				AMFContext2Ptr(AmfContext)->InitDX12(GDynamicRHI->RHIGetNativeDevice());
			}
			else if (RHIName == "Vulkan")
			{
				AMFContext1Ptr(AmfContext)->InitVulkan(GDynamicRHI->RHIGetNativeDevice());
			}

			bIsCtxInitialized = true;
		}

        bIsAvailable = true;
    }
}