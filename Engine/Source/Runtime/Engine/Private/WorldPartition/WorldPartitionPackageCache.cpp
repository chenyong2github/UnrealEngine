// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionPackageCache.h"

#if WITH_EDITOR
#include "UObject/UObjectHash.h"
#include "Engine/World.h"
#include "PackageTools.h"
#include "Misc/PackagePath.h"

FWorldPartitionPackageCache::FWorldPartitionPackageCache()
{}

FWorldPartitionPackageCache::~FWorldPartitionPackageCache()
{
	check(!LoadingPackages.Num());
	UnloadPackages();
}

void FWorldPartitionPackageCache::TrashPackage(UPackage* InPackage)
{	
	CachedPackages.Remove(InPackage->GetFName());
	UnloadPackage(InPackage);

	// Rename so it isn't found again
	FName NewUniqueTrashName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), FName(*FString::Printf(TEXT("%s_Trashed"), *InPackage->GetName())));
	InPackage->Rename(*NewUniqueTrashName.ToString(), nullptr, REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional | REN_DoNotDirty);
}

void FWorldPartitionPackageCache::UnloadPackages()
{
	if (CachedPackages.Num())
	{
		for (const auto& Pair : CachedPackages)
		{
			if (UPackage* CachedPackage = Pair.Value.Get())
			{
				UnloadPackage(CachedPackage);
			}
		}
				
		CachedPackages.Empty();
	}
}

void FWorldPartitionPackageCache::UnloadPackage(UPackage* InPackage)
{
	check(InPackage);
	ForEachObjectWithPackage(InPackage, [](UObject* Object)
	{
		Object->ClearFlags(RF_Standalone);
		return true;
	}, false);

	// World specific
	if (UWorld* PackageWorld = UWorld::FindWorldInPackage(InPackage))
	{
		if (PackageWorld->PersistentLevel)
		{
			// Manual cleanup of level since world was not initialized
			PackageWorld->PersistentLevel->CleanupLevel(/*bCleanupResources*/ true);
		}
	}
}

void FWorldPartitionPackageCache::LoadPackage(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, bool bLoadAsync, bool bInWorldPackage)
{
	if (UPackage* CachedPackage = FindPackage(InPackageName))
	{
		InCompletionDelegate.Execute(InPackageName, CachedPackage, EAsyncLoadingResult::Succeeded);
		return;
	}

	// Find loading package
	if (TArray<FLoadPackageAsyncDelegate>* CompletionDelegates = LoadingPackages.Find(InPackageName))
	{
		CompletionDelegates->Add(InCompletionDelegate);
		return;
	}

	// Not found start loading
	LoadingPackages.Add(InPackageName, { InCompletionDelegate });

	FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([this, bInWorldPackage](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
	{
		if (Result == EAsyncLoadingResult::Succeeded)
		{
			CachedPackages.Add(LoadedPackageName, LoadedPackage);
		}

		if (bInWorldPackage)
		{
			if (UWorld* PackageWorld = UWorld::FindWorldInPackage(LoadedPackage))
			{
				PackageWorld->PersistentLevel->OnLevelLoaded();
			}
		}
	
		TArray<FLoadPackageAsyncDelegate> CompletionDelegates = LoadingPackages.FindAndRemoveChecked(LoadedPackageName);
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

UPackage* FWorldPartitionPackageCache::FindPackage(FName InPackageName)
{
	if (TWeakObjectPtr<UPackage>* CachedPackagePtr = CachedPackages.Find(InPackageName))
	{
		if (UPackage* CachedPackage = CachedPackagePtr->Get())
		{
			return CachedPackage;
		}
		CachedPackages.Remove(InPackageName);
	}

	// Might have been cached by other instance of a FWorldPartitionPackageCache. Happens when cooking WP Cells that each have their own FWorldPartitionPackageCache.
	if(UPackage* Package = ::FindPackage(nullptr, *InPackageName.ToString()))
	{
		CachedPackages.Add(InPackageName, Package);
		return Package;
	}

	return nullptr;
}

UPackage* FWorldPartitionPackageCache::DuplicateWorldPackage(UPackage* InPackage, FName InDuplicatePackageName)
{
	check(!CachedPackages.Contains(InDuplicatePackageName));

	UPackage* DuplicatedPackage = nullptr;
	if (UWorld* PackageWorld = UWorld::FindWorldInPackage(InPackage))
	{
		DuplicatedPackage = CreatePackage(*InDuplicatePackageName.ToString());
		FObjectDuplicationParameters DuplicationParameters(PackageWorld, DuplicatedPackage);
		DuplicationParameters.bAssignExternalPackages = false;
		DuplicationParameters.DuplicateMode = EDuplicateMode::World;

		UWorld* DuplicatedWorld = Cast<UWorld>(StaticDuplicateObjectEx(DuplicationParameters));
		check(DuplicatedWorld);
		CachedPackages.Add(InDuplicatePackageName, DuplicatedPackage);
	}
	
	return DuplicatedPackage;
}

#endif
