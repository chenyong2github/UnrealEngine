// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "GlobalData.h"
#include "Context.h"
#include <mutex>

namespace AutoRTFM
{

std::once_flag GlobalDataInitializationFlag;
FGlobalData* GlobalData;

void InitializeGlobalDataIfNecessary()
{
    std::call_once(GlobalDataInitializationFlag, []
    {
        // AutoRTFM is intended to be used primarily from monolithic UE binaries, where having
        // some additional DLL/so/dylib would just be annoying. However, we guard against the
        // possibility that these things somehow get linked together. In that case, the only
        // bad thing you get is code bloat, but otherwise everything works out:
        //
        // - Each AutoRTFM runtime instance registers functions for whatever version of the
        //   standard library it sees.
        //
        // - All AutoRTFM instances coordinate together on things like the function table,
        //   lock table, and TLS key.
        
        constexpr const char* EnvName = "AutoRTFMGlobalData";

#ifdef _MSC_VER
/*
   Disable warning about deprecated STD C functions.
*/
#pragma warning(disable : 4996)

#pragma warning(push)
#endif

        if (char* EnvString = getenv(EnvName))
        {
            sscanf(EnvString, "%p", &GlobalData);
            ASSERT(GlobalData);
        }
        else
        {
            GlobalData = new FGlobalData;
            FContext::InitializeGlobalData();
            char Buffer[128];
            snprintf(Buffer, sizeof(Buffer), "%s=%p", EnvName, GlobalData);
            putenv(Buffer);
        }

#ifdef _MSC_VER
#pragma warning(pop)
#endif
    });
}

struct FInitializeGlobalData
{
    FInitializeGlobalData()
    {
        InitializeGlobalDataIfNecessary();
    }
};

FInitializeGlobalData InitializeGlobalData;

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
