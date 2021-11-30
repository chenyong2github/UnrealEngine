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

	void LoadPackage(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage);
	UPackage* FindPackage(FName InPackageName);
	UPackage* DuplicateWorldPackage(UPackage* InPackage, FName InPackageName);
	void UnloadPackages();
	void TrashPackage(UPackage* InPackage);
	
private:

	void UnloadPackage(UPackage* InPackage);

	TMap<FName, TWeakObjectPtr<UPackage>> CachedPackages;
	TMap<FName, TArray<FLoadPackageAsyncDelegate>> LoadingPackages;
};

#endif