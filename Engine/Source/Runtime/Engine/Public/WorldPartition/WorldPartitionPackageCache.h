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
	~FWorldPartitionPackageCache();
	void LoadPackage(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage);
	void UnloadPackages();

	// @todo_ow: This should be removed as soon as FWorldPartitionLevelHelper::LoadActors doesn't do package duplication
	void CacheLoadedPackage(UPackage* InPackage) { LocalCache.Add(InPackage); }
private:
	TSet<TWeakObjectPtr<UPackage>> LocalCache;
};

class FWorldPartitionPackageGlobalCache
{
public:
	static FWorldPartitionPackageGlobalCache& Get() { return GlobalCache; }

	void LoadPackage(FWorldPartitionPackageCache* Client, FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage);
	void UnloadPackages(FWorldPartitionPackageCache* Client);

private:

	struct FPackageCacheInfo
	{
		bool HasPendingLoad() const { return CompletionDelegates.Num() > 0; }
		TSet<FWorldPartitionPackageCache*> Referencers;
		TWeakObjectPtr<UPackage> Package;
		TArray<FLoadPackageAsyncDelegate> CompletionDelegates;
	};
	TMap<FName, FPackageCacheInfo> Cache;

	static FWorldPartitionPackageGlobalCache GlobalCache;
};

#endif