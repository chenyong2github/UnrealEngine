// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "Misc/Base64.h"
#endif

UActorDescContainer::UActorDescContainer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
#if WITH_EDITOR
	, bContainerInitialized(false)
#endif
{}

void UActorDescContainer::Initialize(UWorld* InWorld, FName InPackageName, bool bRegisterDelegates)
{
	check(!World || World == InWorld);
	World = InWorld;
#if WITH_EDITOR
	check(!bContainerInitialized);
	ContainerPackageName = InPackageName;
	TArray<FAssetData> Assets;
	
	if (!ContainerPackageName.IsNone())
	{
		const FString LevelPathStr = ContainerPackageName.ToString();
		const FString LevelExternalActorsPath = ULevel::GetExternalActorsPath(LevelPathStr);

		// Do a synchronous scan of the level external actors path.			
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.ScanPathsSynchronous({ LevelExternalActorsPath }, /*bForceRescan*/true, /*bIgnoreBlackListScanFilters*/true);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.PackagePaths.Add(*LevelExternalActorsPath);

		AssetRegistry.GetAssets(Filter, Assets);
	}

	for (const FAssetData& Asset : Assets)
	{
		TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = new(ActorDescList) TUniquePtr<FWorldPartitionActorDesc>(GetActorDescriptor(Asset));
		if (NewActorDesc->IsValid())
		{
			Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);
		}
	}

	if (bRegisterDelegates)
	{
		RegisterDelegates();
	}

	bContainerInitialized = true;
#endif
}

void UActorDescContainer::Uninitialize()
{
	World = nullptr;
#if WITH_EDITOR
	if (bContainerInitialized)
	{
		UnregisterDelegates();
		bContainerInitialized = false;
	}

	for (TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		ActorDescPtr.Reset();
	}
#endif
}

UWorld* UActorDescContainer::GetWorld() const
{
	if (World)
	{
		return World;
	}
	return Super::GetWorld();
}

void UActorDescContainer::BeginDestroy()
{
	Super::BeginDestroy();

	Uninitialize();
}

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc> UActorDescContainer::GetActorDescriptor(const FAssetData& InAssetData)
{
	FString ActorClassName;
	static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
	if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorClassName))
	{
		FString ActorMetaDataStr;
		static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
		if (InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr))
		{
			bool bIsValidClass = true;
			UClass* ActorClass = FindObject<UClass>(ANY_PACKAGE, *ActorClassName, true);

			if (!ActorClass)
			{
				ActorClass = AActor::StaticClass();
				bIsValidClass = false;
			}

			FWorldPartitionActorDescInitData ActorDescInitData;
			ActorDescInitData.NativeClass = ActorClass;
			ActorDescInitData.PackageName = InAssetData.PackageName;
			ActorDescInitData.ActorPath = InAssetData.ObjectPath;
			FBase64::Decode(ActorMetaDataStr, ActorDescInitData.SerializedData);

			TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::CreateClassActorDesc(ActorDescInitData.NativeClass));

			NewActorDesc->Init(this, ActorDescInitData);
			
			check(bIsValidClass || (NewActorDesc->GetActorIsEditorOnly() && IsRunningGame()));
			return NewActorDesc;
		}
	}

	return nullptr;
}

bool UActorDescContainer::ShouldHandleActorEvent(const AActor* Actor)
{
	if (Actor && Actor->IsPackageExternal() && (Actor->GetLevel() == World->PersistentLevel))
	{
		// Only handle assets that belongs to our level
		auto RemoveAfterFirstDot = [](const FString& InValue)
		{
			int32 DotIndex;
			if (InValue.FindChar(TEXT('.'), DotIndex))
			{
				return InValue.LeftChop(InValue.Len() - DotIndex);
			}
			return InValue;
		};

		const FString ThisLevelPath = ContainerPackageName.ToString();
		const FString AssetLevelPath = RemoveAfterFirstDot(Actor->GetPathName());
		return (ThisLevelPath == AssetLevelPath);
	}

	return false;
}

void UActorDescContainer::OnObjectPreSave(UObject* Object)
{
	if (const AActor* Actor = Cast<AActor>(Object))
	{
		if (ShouldHandleActorEvent(Actor))
		{
			if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindRef(Actor->GetActorGuid()))
			{
				// Pin the actor handle on the actor to prevent unloading it when unhashing
				FWorldPartitionHandle ExistingActorHandle(ExistingActorDesc);
				FWorldPartitionHandlePinRefScope ExistingActorHandlePin(ExistingActorHandle);

				TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(Actor->CreateActorDesc());

				OnActorDescUpdating(*ExistingActorDesc);

				// Transfer any reference count from external sources
				NewActorDesc->TransferRefCounts(ExistingActorDesc->Get());

				*ExistingActorDesc = MoveTemp(NewActorDesc);

				OnActorDescUpdated(*ExistingActorDesc);
			}
			// New actor
			else
			{
				TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = new(ActorDescList) TUniquePtr<FWorldPartitionActorDesc>(Actor->CreateActorDesc());
				check(NewActorDesc->IsValid());

				check(!Actors.Contains((*NewActorDesc)->GetGuid()));
				Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);

				OnActorDescAdded(*NewActorDesc);
			}
		}
	}
}

void UActorDescContainer::OnPackageDeleted(UPackage* Package)
{
	AActor* Actor = nullptr;
	ForEachObjectWithPackage(Package, [&Actor](UObject* Object)	{ Actor = Cast<AActor>(Object);	return !Actor; }, false);

	if (ShouldHandleActorEvent(Actor))
	{
		if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindRef(Actor->GetActorGuid()))
		{
			OnActorDescRemoved(*ExistingActorDesc);
			Actors.Remove((*ExistingActorDesc)->GetGuid());
			ExistingActorDesc->Release();
		}
	}
}

void UActorDescContainer::RegisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		FCoreUObjectDelegates::OnObjectSaved.AddUObject(this, &UActorDescContainer::OnObjectPreSave);
		FEditorDelegates::OnPackageDeleted.AddUObject(this, &UActorDescContainer::OnPackageDeleted);
	}
}

void UActorDescContainer::UnregisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		FCoreUObjectDelegates::OnObjectSaved.RemoveAll(this);
		FEditorDelegates::OnPackageDeleted.RemoveAll(this);
	}
}
#endif