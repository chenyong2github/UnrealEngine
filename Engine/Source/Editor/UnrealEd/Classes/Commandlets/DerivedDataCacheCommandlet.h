// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DerivedDataCacheCommandlet.cpp: Commandlet for DDC maintenence
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "DerivedDataCacheCommandlet.generated.h"

UCLASS()
class UDerivedDataCacheCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()
	UDerivedDataCacheCommandlet(FVTableHelper& Helper); // Declare the FVTableHelper constructor manually so that we can forward-declare-only TUniquePtrs in the header without getting compile error in generated cpp

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	// Objects currently being cached along with when was the last time in seconds we verified if they were still compiling
	// The last time is used to do some throttling on the IsCachedCookedPlatformDataLoaded which can be quite expensive on some objects
	TMap<UObject*, double> CachingObjects;
	TSet<FName>    ProcessedPackages;
	TSet<FName>    PackagesToProcess;
	double FinishCacheTime = 0.0;
	double BeginCacheTime = 0.0;

	class FPackageListener;
	TUniquePtr<FPackageListener> PackageListener;

	class FObjectReferencer;
	TUniquePtr<FObjectReferencer> ObjectReferencer;

	void MaybeMarkPackageAsAlreadyLoaded(UPackage *Package);

	void CacheLoadedPackages(UPackage* CurrentPackage, uint8 PackageFilter, const TArray<ITargetPlatform*>& Platforms);
	bool ProcessCachingObjects(const TArray<ITargetPlatform*>& Platforms);
	void FinishCachingObjects(const TArray<ITargetPlatform*>& Platforms);
};


