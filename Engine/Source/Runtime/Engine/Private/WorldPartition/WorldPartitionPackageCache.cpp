// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionPackageCache.h"

#if WITH_EDITOR
#include "UObject/UObjectHash.h"
#include "Engine/World.h"
#include "PackageTools.h"
#include "Misc/PackagePath.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionPackageHelper.h"

/*
 * FWorldPartitionPackageGlobalCache
 */

FWorldPartitionPackageGlobalCache FWorldPartitionPackageGlobalCache::GlobalCache;

void FWorldPartitionPackageGlobalCache::UnloadPackages(FWorldPartitionPackageCache* InClient)
{
	for (auto It = Cache.CreateIterator(); It; ++It)
	{
		FPackageCacheInfo& CacheInfo = It->Value;
		CacheInfo.Referencers.Remove(InClient);
		if (CacheInfo.Referencers.Num() == 0)
		{
			check(!CacheInfo.HasPendingLoad());
			if (UPackage* CachedPackage = CacheInfo.Package.Get())
			{
				FWorldPartitionPackageHelper::UnloadPackage(CachedPackage);
			}
			It.RemoveCurrent();
		}
	}
}

void FWorldPartitionPackageGlobalCache::LoadPackage(FWorldPartitionPackageCache* InClient, FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage)
{
	FPackageCacheInfo& CacheInfo = Cache.FindOrAdd(InPackageName);
	CacheInfo.Referencers.Add(InClient);

	if (UPackage* CachedPackage = CacheInfo.Package.Get())
	{
		InCompletionDelegate.Execute(InPackageName, CachedPackage, EAsyncLoadingResult::Succeeded);
		return;
	}
	check(!::FindPackage(nullptr, *InPackageName.ToString()));

	// If request already pending, simply push the delegate
	if (CacheInfo.HasPendingLoad())
	{
		CacheInfo.CompletionDelegates.Add(InCompletionDelegate);
		return;
	}

	// Not found start loading
	CacheInfo.CompletionDelegates.Add(InCompletionDelegate);

	FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([this, bInWorldPackage, InClient](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
	{
		FPackageCacheInfo& CacheInfo = Cache.FindChecked(LoadedPackageName);
		check(CacheInfo.ScopedLoadAllExternalObjects.IsValid());
		CacheInfo.ScopedLoadAllExternalObjects.Reset();

		if (Result == EAsyncLoadingResult::Succeeded)
		{
			check(CacheInfo.Package.Get() == nullptr);
			CacheInfo.Package = LoadedPackage;
		}

		if (bInWorldPackage)
		{
			if (UWorld* PackageWorld = UWorld::FindWorldInPackage(LoadedPackage))
			{
				check(PackageWorld->HasAnyFlags(RF_Standalone));
				PackageWorld->PersistentLevel->OnLevelLoaded();
			}
		}
	
		check(CacheInfo.HasPendingLoad());
		TArray<FLoadPackageAsyncDelegate> CompletionDelegates = MoveTemp(CacheInfo.CompletionDelegates);
		for (FLoadPackageAsyncDelegate& CompletionDelegate : CompletionDelegates)
		{
			CompletionDelegate.Execute(LoadedPackageName, LoadedPackage, Result);
		}
	});

	// This is to prevent the world to be initialized (when a World asset is added and its type is EWorldType::Inactive it gets initialized)
	if (bInWorldPackage)
	{
		UWorld::WorldTypePreLoadMap.FindOrAdd(InPackageName) = EWorldType::Editor;
	}

	// Cache doesn't initialize the world, so make sure external objects will be loaded in ULevel::PostLoad
	CacheInfo.ScopedLoadAllExternalObjects = TUniquePtr<FScopedLoadAllExternalObjects>(new FScopedLoadAllExternalObjects(InPackageName));
	if (bLoadAsync)
	{
		::LoadPackageAsync(FPackagePath::FromPackageNameChecked(InPackageToLoadFrom), InPackageName, CompletionCallback);
	}
	else
	{
		UPackage* Package = CreatePackage(*InPackageName.ToString());
		Package = ::LoadPackage(Package, InPackageToLoadFrom, LOAD_None);
		CompletionCallback.Execute(InPackageName, Package, Package ? EAsyncLoadingResult::Succeeded : EAsyncLoadingResult::Failed);
	}
}

/*
 * FWorldPartitionPackageCache
 */

FWorldPartitionPackageCache::~FWorldPartitionPackageCache()
{
	UnloadPackages();
	
}

void FWorldPartitionPackageCache::LoadPackage(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage)
{
	FWorldPartitionPackageGlobalCache::Get().LoadPackage(this, InPackageName, InPackageToLoadFrom, InCompletionDelegate, bLoadAsync, bInWorldPackage);
}

void FWorldPartitionPackageCache::UnloadPackages()
{
	FWorldPartitionPackageGlobalCache::Get().UnloadPackages(this);

	for (TWeakObjectPtr<UPackage>& Package : LocalCache)
	{
		if (UPackage* CachedPackage = Package.Get())
		{
			FWorldPartitionPackageHelper::UnloadPackage(CachedPackage);
		}
	}
}

#endif