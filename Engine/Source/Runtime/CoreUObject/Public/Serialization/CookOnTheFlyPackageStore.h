// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_COTF

#include "CoreMinimal.h"
#include "IO/PackageStore.h"

class FIoDispatcher;

namespace UE { namespace Cook
{
	class ICookOnTheFlyServerConnection;
}}

namespace UE { namespace PackageStore { namespace Messaging
{

struct FPackageStoreData
{
	TArray<FPackageStoreEntryResource> CookedPackages;
	TArray<FPackageId> FailedPackages;
	int32 TotalCookedPackages = 0;
	int32 TotalFailedPackages = 0;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FPackageStoreData& PackageStoreData);
};

struct FCookPackageRequest
{
	FName PackageName;
	
	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FCookPackageRequest& Request);
};

struct FCookPackageResponse
{
	EPackageStoreEntryStatus Status = EPackageStoreEntryStatus::None;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FCookPackageResponse& Response);
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

}}} // namesapce UE::PackageStore::Messaging

TUniquePtr<IPackageStore> MakeCookOnTheFlyPackageStore(
	FIoDispatcher& IoDispatcher,
	UE::Cook::ICookOnTheFlyServerConnection& CookOnTheFlyServerConnection,
	TFunction<void()>&& EntriesAddedCallback);

#endif // WITH_COTF
