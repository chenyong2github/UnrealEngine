// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/DDCCleanupCommandlet.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheMaintainer.h"
#include "HAL/PlatformProcess.h"

int32 UDDCCleanupCommandlet::Main(const FString& Params)
{
	using namespace UE::DerivedData;
	ICacheStoreMaintainer& Maintainer = GetCache().GetMaintainer();
	Maintainer.BoostPriority();
	while (!Maintainer.IsIdle())
	{
		// Maintenance works from dedicated threads. Wait for it to finish and flush logs occasionally.
		FPlatformProcess::SleepNoStats(0.05f);
		GLog->Flush();
	}
	return 0;
}
