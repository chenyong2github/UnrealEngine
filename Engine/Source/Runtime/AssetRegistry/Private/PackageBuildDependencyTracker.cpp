// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageBuildDependencyTracker.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "UObject/Package.h"
#include "Misc/PackageAccessTrackingOps.h"

#if UE_WITH_PACKAGE_ACCESS_TRACKING

DEFINE_LOG_CATEGORY_STATIC(LogPackageBuildDependencyTracker, Log, All);

FPackageBuildDependencyTracker FPackageBuildDependencyTracker::Singleton;

void FPackageBuildDependencyTracker::DumpData() const
{
	FScopeLock RecordsScopeLock(&RecordsLock);
	uint64 ReferencingPackageCount = 0;
	uint64 ReferenceCount = 0;
	for (const auto& PackageAccessRecord : Records)
	{
		++ReferencingPackageCount;
		for (const auto& AccessedData : PackageAccessRecord.Value)
		{
			++ReferenceCount;
		}
	}
	UE_LOG(LogPackageBuildDependencyTracker, Display, TEXT("Package Accesses (%u referencing packages with a total of %u unique accesses)"), ReferencingPackageCount, ReferenceCount);
#if 0
	UE_LOG(LogPackageBuildDependencyTracker, Display, TEXT("========================================================================="));
	for (const auto& PackageAccessRecord : Records)
	{
		UE_LOG(LogPackageBuildDependencyTracker, Display, TEXT("%s:"), *PackageAccessRecord.Key.ToString());
		for (const auto& AccessedData : PackageAccessRecord.Value)
		{
			UE_LOG(LogPackageBuildDependencyTracker, Display, TEXT("    %s"), *AccessedData.ToString());
		}
	}
#endif
}

FPackageBuildDependencyTracker::FPackageBuildDependencyTracker()
{
	PreviousObjectHandleReadFunction = SetObjectHandleReadCallback(StaticOnObjectHandleRead);
}

void FPackageBuildDependencyTracker::StaticOnObjectHandleRead(UObject* ReadObject)
{
	if (ReadObject)
	{
		if (PackageAccessTracking_Private::FPackageAccessRefScope* InnermostThreadScope = PackageAccessTracking_Private::FPackageAccessRefScope::GetCurrentThreadScope())
		{
			PackageAccessTracking_Private::FPackageAccessRefScope* SearchThreadScope = InnermostThreadScope;
			
			do
			{
				FName OpName = SearchThreadScope->GetOpName();
				if ((OpName == PackageAccessTrackingOps::NAME_Load) || (OpName == PackageAccessTrackingOps::NAME_Save) /*|| (OpName == PackageAccessTrackingOps::NAME_CreateDefaultObject)*/)
				{
					//If this was an Package Access done in the context of a save or load, we record it as a build dependency.
					//If we want to capture script package dependencies, we may also need to record accesses under the "PackageAccessTrackingOps::NAME_CreateDefaultObject" operation
					//which occurs from "UObjectLoadAllCompiledInDefaultProperties" and possibly elsewhere.

					//NOTE: The referencer is the InnermostThreadScope, not the SearchThreadScope.  Consider a situation where the in the process of loading package A,
					//		we call PostLoad on an object on package B which references an object in package C.  The SearchThreadScope with OpName == Load is for package A,
					//		but we want to record that B depends on C, not that A depends on C.  The InnermostThreadScope will have a package name of B and an OpName of PostLoad
					//		and so we want the package name from the InnermostThreadScope while having searched upwards for a scope with an OpName == Load.
					FName Referencer = InnermostThreadScope->GetPackageName();
					FName Referenced = ReadObject->GetOutermost()->GetFName();
					if (Referencer != Referenced)
					{
						FScopeLock RecordsScopeLock(&Singleton.RecordsLock);
						Singleton.Records.FindOrAdd(Referencer).Add(Referenced);
					}
					break;
				}
				SearchThreadScope = SearchThreadScope->GetOuter();
			} while (SearchThreadScope);
		}
	}

	if (Singleton.PreviousObjectHandleReadFunction)
	{
		Singleton.PreviousObjectHandleReadFunction(ReadObject);
	}
}

#endif // UE_WITH_OBJECT_HANDLE_TRACKING
