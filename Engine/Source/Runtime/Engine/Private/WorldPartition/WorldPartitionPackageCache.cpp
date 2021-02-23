// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionPackageCache.h"

#if WITH_EDITOR
#include "UObject/UObjectHash.h"
#include "Engine/World.h"
#include "PackageTools.h"
#include "Misc/PackagePath.h"

FWorldPartitionPackageCache::FWorldPartitionPackageCache()
{
	
}

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
		if (PackageWorld->PersistentLevel && PackageWorld->PersistentLevel->IsUsingExternalActors())
		{
			for (AActor* Actor : PackageWorld->PersistentLevel->Actors)
			{
				if (UPackage* ActorPackage = Actor ? Actor->GetExternalPackage() : nullptr)
				{
					ForEachObjectWithPackage(ActorPackage, [&](UObject* Object)
						{
							Object->ClearFlags(RF_Standalone);
							return true;
						}, false);
				}
			}
		}
	}
}

void FWorldPartitionPackageCache::LoadWorldPackageAsync(FName InPackageName, const TCHAR* InPackageToLoadFrom /*= nullptr*/, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/, const FLinkerInstancingContext* InInstancingContext /*=nullptr*/)
{
	LoadPackageAsyncInternal(InPackageName, InPackageToLoadFrom, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InInstancingContext, /* bInWorldPackage=*/true);
}

void FWorldPartitionPackageCache::LoadPackageAsync(FName InPackageName, const TCHAR* InPackageToLoadFrom /*= nullptr*/, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/, const FLinkerInstancingContext* InInstancingContext /*=nullptr*/)
{
	LoadPackageAsyncInternal(InPackageName, InPackageToLoadFrom, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InInstancingContext, /* bInWorldPackage=*/false);
}

void FWorldPartitionPackageCache::LoadPackageAsyncInternal(FName InPackageName, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority, const FLinkerInstancingContext* InInstancingContext, bool bInWorldPackage)
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

	FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([this](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			if (Result == EAsyncLoadingResult::Succeeded)
			{
				CachedPackages.Add(LoadedPackageName, LoadedPackage);
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
	::LoadPackageAsync(FPackagePath::FromPackageNameChecked(InPackageToLoadFrom), InPackageName, CompletionCallback, nullptr, InPackageFlags, InPIEInstanceID, InPackagePriority, InInstancingContext);
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