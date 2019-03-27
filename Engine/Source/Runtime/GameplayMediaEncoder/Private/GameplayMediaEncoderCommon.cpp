// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayMediaEncoderCommon.h"
#include "RHI.h"

#if WMFMEDIA_SUPPORTED_PLATFORM
	#pragma comment(lib, "mfplat")
	#pragma comment(lib, "mfuuid")
	#pragma comment(lib, "Mfreadwrite")
#endif

GAMEPLAYMEDIAENCODER_START

//
// Windows only code
//
#if PLATFORM_WINDOWS
ID3D11Device* GetUE4DxDevice()
{
	auto Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	checkf(Device != nullptr, TEXT("Failed to get UE4's ID3D11Device"));
	return Device;
}
#endif

//
// XboxOne only code
// 
#if PLATFORM_XBOXONE 
ID3D12Device* GetUE4DxDevice()
{
	auto Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
	checkf(Device != nullptr, TEXT("Failed to get UE4's ID3D12Device"));
	return Device;
}

#endif


// #RVF : Remove these once the code is production ready
TArray<FMemoryCheckpoint> gMemoryCheckpoints;
uint64 MemoryCheckpoint(const FString& Name)
{
#if PLATFORM_WINDOWS
	return 0;
#else
	TITLEMEMORYSTATUS TitleStatus;
	TitleStatus.dwLength = sizeof(TitleStatus);
	TitleMemoryStatus(&TitleStatus);

	static uint64 PeakMemory = 0;

	FMemoryCheckpoint Check;
	Check.Name = Name;
	uint64_t UsedPhysical = TitleStatus.ullLegacyUsed + TitleStatus.ullTitleUsed;
	static uint64_t FirstUsedPhysical = UsedPhysical;
	
	Check.UsedPhysicalMB = UsedPhysical / double(1024 * 1024);
	Check.DeltaMB = 0;
	Check.AccumulatedMB = (UsedPhysical - FirstUsedPhysical) / double(1024 * 1024);
	if (gMemoryCheckpoints.Num())
	{
		Check.DeltaMB = Check.UsedPhysicalMB - gMemoryCheckpoints.Last().UsedPhysicalMB;
	}
	gMemoryCheckpoints.Add(Check);
	return UsedPhysical;
#endif
}

void LogMemoryCheckpoints(const FString& Name)
{
	UE_LOG(GameplayMediaEncoder, Log, TEXT("Memory breakdown: %s..."), *Name);
	for (const FMemoryCheckpoint& a : gMemoryCheckpoints)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("%s: UsedPhysicalMB=%4.3f, DeltaMB=%4.3f, AccumulatedMB=%4.3f"),
			*a.Name, a.UsedPhysicalMB, a.DeltaMB, a.AccumulatedMB);
	}
}


GAMEPLAYMEDIAENCODER_END

