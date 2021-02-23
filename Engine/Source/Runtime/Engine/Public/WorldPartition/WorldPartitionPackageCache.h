// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Guid.h"

class FLinkerInstancingContext;

class FWorldPartitionPackageCache
{
public:
	FWorldPartitionPackageCache();
	~FWorldPartitionPackageCache();

	void LoadWorldPackageAsync(FName InPackageName, const TCHAR* InPackageToLoadFrom = nullptr, FLoadPackageAsyncDelegate InCompletionDelegate = FLoadPackageAsyncDelegate(), EPackageFlags InPackageFlags = PKG_None, int32 InPIEInstanceID = INDEX_NONE, int32 InPackagePriority = 0, const FLinkerInstancingContext* InInstancingContext = nullptr);
	void LoadPackageAsync(FName InPackageName, const TCHAR* InPackageToLoadFrom = nullptr, FLoadPackageAsyncDelegate InCompletionDelegate = FLoadPackageAsyncDelegate(), EPackageFlags InPackageFlags = PKG_None, int32 InPIEInstanceID = INDEX_NONE, int32 InPackagePriority = 0, const FLinkerInstancingContext* InInstancingContext = nullptr);
	UPackage* FindPackage(FName InPackageName);
	UPackage* DuplicateWorldPackage(UPackage* InPackage, FName InPackageName);
	void UnloadPackages();
	void TrashPackage(UPackage* InPackage);
	
private:
	void LoadPackageAsyncInternal(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority, const FLinkerInstancingContext* InInstancingContext, bool bInWorldPackage);
	void UnloadPackage(UPackage* InPackage);

	TMap<FName, TWeakObjectPtr<UPackage>> CachedPackages;
	TMap<FName, TArray<FLoadPackageAsyncDelegate>> LoadingPackages;
};

#endif