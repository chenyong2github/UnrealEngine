// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_COTF

#include "IO/PackageStore.h"

namespace UE { namespace ZenCookOnTheFly { namespace Messaging
{

struct FCompletedPackages
{
	TArray<FPackageStoreEntryResource> CookedPackages;
	TArray<FPackageId> FailedPackages;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCompletedPackages& CompletedPackages);
};

struct FCookPackageRequest
{
	FPackageId PackageId;
	
	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookPackageRequest& Request);
};

struct FCookPackageResponse
{
	EPackageStoreEntryStatus Status;
	FPackageStoreEntryResource CookedEntry;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookPackageResponse& Response);
};

struct FRecookPackagesRequest
{
	TArray<FPackageId> PackageIds;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FRecookPackagesRequest& Request);
};

struct FRecookPackagesResponse
{
	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FRecookPackagesResponse& Response);
};

}}} // namesapce UE::ZenCookOnTheFly::Messaging

#endif // WITH_COTF
