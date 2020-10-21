// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/CriticalSection.h"
#include "Misc/PackageAccessTracking.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ObjectHandle.h"

#if UE_WITH_PACKAGE_ACCESS_TRACKING

class FPackageBuildDependencyTracker: public FNoncopyable
{
public:
	static ASSETREGISTRY_API FPackageBuildDependencyTracker& Get() { return Singleton; }

	ASSETREGISTRY_API void DumpData() const;

private:
	FPackageBuildDependencyTracker();

	/** Track object reference reads */
	static void StaticOnObjectHandleRead(UObject* ReadObject);
	ObjectHandleReadFunction* PreviousObjectHandleReadFunction = nullptr;

	mutable FCriticalSection RecordsLock;
	TMap<FName, TSet<FName>> Records;
	static FPackageBuildDependencyTracker Singleton;
};

ASSETREGISTRY_API void DumpBuildDependencyTrackerData()
{
	FPackageBuildDependencyTracker::Get().DumpData();
}

#else

ASSETREGISTRY_API void DumpBuildDependencyTrackerData()
{
}

#endif // UE_WITH_PACKAGE_ACCESS_TRACKING
