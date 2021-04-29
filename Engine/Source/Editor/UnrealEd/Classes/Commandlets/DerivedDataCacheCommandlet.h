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
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	// We hook this up to a delegate to avoid reloading textures and whatnot
	TSet<FName> ProcessedPackages;
	TSet<FName> PackagesToProcess;
	double FinishCacheTime = 0.0;
	double BeginCacheTime = 0.0;

	void MaybeMarkPackageAsAlreadyLoaded(UPackage *Package);

	void CacheLoadedPackages(UPackage* CurrentPackage, uint8 PackageFilter, const TArray<ITargetPlatform*>& Platforms);
	void CacheWorldPackages(UWorld* World, uint8 PackageFilter, const TArray<ITargetPlatform*>& Platforms);
};


