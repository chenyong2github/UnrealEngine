// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_COTF

#include "IO/PackageStore.h"

namespace UE { namespace ZenCookOnTheFly { namespace Messaging
{

struct FPackageStoreData
{
	TArray<FPackageStoreEntryResource> CookedPackages;
	TArray<FPackageId> FailedPackages;
	int32 TotalCookedPackages = 0;
	int32 TotalFailedPackages = 0;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FPackageStoreData& PackageStoreData);
};

struct FCookPackageRequest
{
	FPackageId PackageId;
	
	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookPackageRequest& Request);
};

struct FCookPackageResponse
{
	EPackageStoreEntryStatus Status = EPackageStoreEntryStatus::None;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookPackageResponse& Response);
};

struct FPackagesCookedMessage
{
	FPackageStoreData PackageStoreData;

	friend FArchive& operator<<(FArchive& Ar, FPackagesCookedMessage& PackagesCookedMessage)
	{
		return Ar << PackagesCookedMessage.PackageStoreData;
	}
};

struct FGetCookedPackagesResponse
{
	FPackageStoreData PackageStoreData;
	
	friend FArchive& operator<<(FArchive& Ar, FGetCookedPackagesResponse& GetCookedPackagesResponse)
	{
		return Ar << GetCookedPackagesResponse.PackageStoreData;
	}
};

}}} // namesapce UE::ZenCookOnTheFly::Messaging

#endif // WITH_COTF
