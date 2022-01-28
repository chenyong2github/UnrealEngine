// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFlyMessages.h"

#if WITH_COTF

namespace UE { namespace ZenCookOnTheFly { namespace Messaging
{

FArchive& operator<<(FArchive& Ar, FPackageStoreData& PackageStoreData)
{
	Ar << PackageStoreData.CookedPackages;
	Ar << PackageStoreData.FailedPackages;
	Ar << PackageStoreData.TotalCookedPackages;
	Ar << PackageStoreData.TotalFailedPackages;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCookPackageRequest& Request)
{
	Ar << Request.PackageId;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCookPackageResponse& Response)
{
	uint32 Status = static_cast<uint32>(Response.Status);
	Ar << Status;

	if (Ar.IsLoading())
	{
		Response.Status = static_cast<EPackageStoreEntryStatus>(Status);
	}

	return Ar;
}

}}} // namesapce UE::ZenCookOnTheFly::Messaging

#endif // WITH_COTF
