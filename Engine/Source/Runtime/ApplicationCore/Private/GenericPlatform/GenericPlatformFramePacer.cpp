// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformFramePacer.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/IConsoleManager.h"

int32 FGenericPlatformRHIFramePacer::GetFramePaceFromSyncInterval()
{
	static IConsoleVariable* SyncIntervalCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rhi.SyncInterval"));
	if (ensure(SyncIntervalCVar != nullptr))
	{
		int SyncInterval = SyncIntervalCVar->GetInt();
		if (SyncInterval > 0)
		{
			return 60 / SyncInterval;
		}
	}

	return 0;
}

int32 FGenericPlatformRHIFramePacer::GetFramePace()
{
	int32 SyncIntervalFramePace = GetFramePaceFromSyncInterval();
	return SyncIntervalFramePace;
}

bool FGenericPlatformRHIFramePacer::SupportsFramePace(int32 QueryFramePace)
{
	// Support frame rates that are an integer multiple of max refresh rate, or 0 for no pacing
	return QueryFramePace == 0 || (60 % QueryFramePace) == 0;
}

int32 FGenericPlatformRHIFramePacer::SetFramePace(int32 InFramePace)
{
	int32 NewFramePace = SetFramePaceToSyncInterval(InFramePace);
	return NewFramePace;
}

int32 FGenericPlatformRHIFramePacer::SetFramePaceToSyncInterval(int32 InFramePace)
{
	int32 NewPace = 0;

	// Disable frame pacing if an unsupported frame rate is requested
	if (!SupportsFramePace(InFramePace))
	{
		InFramePace = 0;
	}

	static IConsoleVariable* SyncIntervalCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rhi.SyncInterval"));
	if (ensure(SyncIntervalCVar != nullptr))
	{
		int32 NewSyncInterval = InFramePace > 0 ? 60 / InFramePace : 0;

		SyncIntervalCVar->Set(NewSyncInterval, ECVF_SetByCode);

		if (NewSyncInterval > 0)
		{
			NewPace = 60 / NewSyncInterval;
		}
	}

	return NewPace;
}


