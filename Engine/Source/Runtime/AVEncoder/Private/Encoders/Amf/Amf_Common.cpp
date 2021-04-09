#include "Amf_Common.h"
#include "AVEncoder.h"

#define CHECK_AMF_RET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (!(Res== AMF_OK || Res==AMF_ALREADY_INITIALIZED))\
	{\
		UE_LOG(LogAVEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
		/*check(false);*/\
		return;\
	}\
}

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

    void FAmfCommon::SetupAmfFunctions()
    {
	    check(!bIsAvailable);
        
        // TODO (M84FIX) Amf setup
        DllHandle = FPlatformProcess::GetDllHandle(UTF8_TO_TCHAR(AMF_DLL_NAME));

        AMFInit_Fn AmfInitFn = (AMFInit_Fn)FPlatformProcess::GetDllExport(DllHandle, UTF8_TO_TCHAR(AMF_INIT_FUNCTION_NAME));
        if (AmfInitFn == nullptr)
        {
            return;
        }

        CHECK_AMF_RET(AmfInitFn(AMF_FULL_VERSION, &AmfFactory));

        CHECK_AMF_RET(AmfFactory->CreateContext(&AmfContext));

        bIsAvailable = true;

    }
}