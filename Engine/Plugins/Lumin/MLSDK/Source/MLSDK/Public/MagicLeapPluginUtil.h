// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CString.h"
#include "Templates/Atomic.h"
#include "Lumin/CAPIShims/LuminAPIPlatform.h"

/** Utility class to deal with some API features. */
namespace MagicLeapAPISetup
{
	/** Returns the, cached, API level for the platform. (thread safe) */
	static FORCEINLINE uint32 GetPlatformLevel()
	{
#if WITH_MLSDK
		// Default to the compiled platform level if we can't get the runtime value.
		static TAtomic<uint32> Level(ML_PLATFORM_API_LEVEL);
		static TAtomic<bool> HaveLevel(false);
		if (!HaveLevel.Load(EMemoryOrder::Relaxed))
		{
			uint32 TempLevel = 0;
			MLResult Result = MLPlatformGetAPILevel(&TempLevel);
			UE_CLOG(Result != MLResult_Ok, LogTemp, Error, TEXT("MLPlatformGetAPILevel() failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			if (Result == MLResult_Ok)
			{
				Level = TempLevel;
			}
			HaveLevel = true;
		}
		return Level;
#else
		return 0;
#endif
	}
};
