// Copyright Epic Games, Inc. All Rights Reserved.

#include "ValidateVirtualizedContentCommandlet.h"

#include "CommandletUtils.h"
#include "UObject/PackageTrailer.h"
#include "Virtualization/VirtualizationSystem.h"

UValidateVirtualizedContentCommandlet::UValidateVirtualizedContentCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UValidateVirtualizedContentCommandlet::Main(const FString& Params)
{
	using namespace UE::Virtualization;

	TRACE_CPUPROFILER_EVENT_SCOPE(UValidateVirtualizedContentCommandlet);

	UE_LOG(LogVirtualization, Display, TEXT("Finding packages in the project..."));
	TArray<FString> PackagePaths = FindPackages(EFindPackageFlags::ExcludeEngineContent);
	UE_LOG(LogVirtualization, Display, TEXT("Found %d package(s)"), PackagePaths.Num());

	TMap<FString, UE::FPackageTrailer> Packages;
	TSet<FIoHash> Payloads;

	UE_LOG(LogVirtualization, Display, TEXT("Scanning package(s) for virtualized payloads..."), PackagePaths.Num());
	FindVirtualizedPayloadsAndTrailers(PackagePaths, Packages, Payloads);
	UE_LOG(LogVirtualization, Display, TEXT("Found %d virtualized package(s) with %d unique payload(s)"), Packages.Num(), Payloads.Num());

	IVirtualizationSystem& System = IVirtualizationSystem::Get();

	UE_LOG(LogVirtualization, Display, TEXT("Querying the state of the virtualized payload(s) in persistent storage..."));
	TArray<EPayloadStatus> PayloadStatuses;
	if (System.QueryPayloadStatuses(Payloads.Array(), EStorageType::Persistent, PayloadStatuses) != EQueryResult::Success)
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to query the statuses of the payload(s)"));
		return 1;
	}

	int32 ErrorCount = 0;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ValidatePackages);

		UE_LOG(LogVirtualization, Display, TEXT("Checking for missing payloads..."));

		for (const TPair<FString, UE::FPackageTrailer>& Package : Packages)
		{
			bool bFoundErrors = false;

			const TArray<FIoHash> VirtualizedPayloads = Package.Value.GetPayloads(UE::EPayloadStorageType::Virtualized);

			for (const FIoHash& PayloadId : VirtualizedPayloads)
			{
				const int32 Index = Payloads.FindId(PayloadId).AsInteger();
				if (PayloadStatuses[Index] == EPayloadStatus::FoundPartial)
				{
					// TODO: We currently don't have a way to inform the user which persistent backend the payload is not in!
					UE_LOG(LogVirtualization, Error, TEXT("%s: Payload '%s' could not be found in all persistent backends"), *Package.Key, *LexToString(PayloadId));
					bFoundErrors = true;
				}
				else if (PayloadStatuses[Index] != EPayloadStatus::FoundAll)
				{
					UE_LOG(LogVirtualization, Error, TEXT("%s: Payload '%s' could not be found in any persistent backend"), *Package.Key, *LexToString(PayloadId));
					bFoundErrors = true;
				}
			}

			if (bFoundErrors)
			{
				ErrorCount++;
			}
		}
	}

	if (ErrorCount == 0)
	{
		UE_LOG(LogVirtualization, Display, TEXT("All virtualized payloads could be found in persistent storage"));
		return 0;
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("%d/%d package(s) had at least one virtualized payload missing from persistent storage"), ErrorCount, Packages.Num());
		return 0;
	}
}
