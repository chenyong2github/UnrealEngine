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
#if WITH_EDITOR
	, bContainerInitialized(false)
	, bIgnoreAssetRegistryEvents(false)
#endif
{
}

void UActorDescContainer::Initialize(FName InPackageName, bool bRegisterDelegates)
{
#if WITH_EDITOR
	check(!bContainerInitialized);
	ContainerPackageName = InPackageName;
	TArray<FAssetData> Assets;
	
	if (!ContainerPackageName.IsNone())
	{
		TGuardValue<bool> IgnoreAssetRegistryEvents(bIgnoreAssetRegistryEvents, true);

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
		check(NewActorDesc->IsValid());
				
		Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);
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
#if WITH_EDITOR
	if (bContainerInitialized)
	{
		UnregisterDelegates();
		bContainerInitialized = false;
	}
#endif
}

void UActorDescContainer::BeginDestroy()
{
	Super::BeginDestroy();

	Uninitialize();
#if WITH_EDITOR
	for (TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		ActorDescPtr.Release();
	}
#endif
}

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc> UActorDescContainer::GetActorDescriptor(const FAssetData& InAssetData)
{
	FString ActorClass;
	static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
	if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorClass))
	{
		FString ActorMetaDataStr;
		static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
		if (InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr))
		{
			FWorldPartitionActorDescInitData ActorDescInitData;
			ActorDescInitData.NativeClass = FindObjectChecked<UClass>(ANY_PACKAGE, *ActorClass, true);
			ActorDescInitData.PackageName = InAssetData.PackageName;
			ActorDescInitData.ActorPath = InAssetData.ObjectPath;
			FBase64::Decode(ActorMetaDataStr, ActorDescInitData.SerializedData);

			TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::CreateClassActorDesc(ActorDescInitData.NativeClass));
			NewActorDesc->Init(this, ActorDescInitData);
			return NewActorDesc;
		}
	}

	return nullptr;
}

bool UActorDescContainer::ShouldHandleAssetEvent(const FAssetData& InAssetData)
{
	// Ignore asset event when specifically asking to
	if (bIgnoreAssetRegistryEvents)
	{
		return false;
	}

	// Ignore in-memory assets until they gets saved
	if (InAssetData.HasAnyPackageFlags(PKG_NewlyCreated))
	{
		return false;
	}

	// Only handle actors
	if (!InAssetData.GetClass()->IsChildOf<AActor>())
	{
		return false;
	}

	// Make sure asset contains the required tags
	static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
	static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
	if (!InAssetData.FindTag(NAME_ActorMetaDataClass) || !InAssetData.FindTag(NAME_ActorMetaData))
	{
		return false;
	}

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
	const FString AssetLevelPath = RemoveAfterFirstDot(InAssetData.ObjectPath.ToString());
	return (ThisLevelPath == AssetLevelPath);
}

void UActorDescContainer::OnAssetAdded(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = new(ActorDescList) TUniquePtr<FWorldPartitionActorDesc>(GetActorDescriptor(InAssetData));
		check(NewActorDesc->IsValid());

		check(!Actors.Contains((*NewActorDesc)->GetGuid()));
		Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);

		OnActorDescAdded(*NewActorDesc);
	}
}

void UActorDescContainer::OnAssetRemoved(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = GetActorDescriptor(InAssetData);
		check(NewActorDesc.IsValid());

		TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindChecked(NewActorDesc->GetGuid());

		OnActorDescRemoved(*ExistingActorDesc);

		Actors.Remove((*ExistingActorDesc)->GetGuid());
		ExistingActorDesc->Release();
	}
}

void UActorDescContainer::OnAssetUpdated(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = GetActorDescriptor(InAssetData);
		check(NewActorDesc.IsValid());

		TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindChecked(NewActorDesc->GetGuid());

		// Pin the actor handle on the actor to prevent unloading it when unhashing
		FWorldPartitionHandle ExistingActorHandle(ExistingActorDesc);
		FWorldPartitionHandlePinRefScope ExistingActorHandlePin(ExistingActorHandle);

		OnActorDescUpdating(*ExistingActorDesc);

		// Transfer any reference count from external sources
		NewActorDesc->TransferRefCounts(ExistingActorDesc->Get());

		*ExistingActorDesc = MoveTemp(NewActorDesc);

		OnActorDescUpdated(*ExistingActorDesc);
	}
}

const FWorldPartitionActorDesc* UActorDescContainer::GetActorDesc(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const * ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

FWorldPartitionActorDesc* UActorDescContainer::GetActorDesc(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>** ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

void UActorDescContainer::RegisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetAdded().AddUObject(this, &UActorDescContainer::OnAssetAdded);
		AssetRegistry.OnAssetRemoved().AddUObject(this, &UActorDescContainer::OnAssetRemoved);
		AssetRegistry.OnAssetUpdated().AddUObject(this, &UActorDescContainer::OnAssetUpdated);
	}
}

void UActorDescContainer::UnregisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
		{
			IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
			AssetRegistry.OnAssetAdded().RemoveAll(this);
			AssetRegistry.OnAssetRemoved().RemoveAll(this);
			AssetRegistry.OnAssetUpdated().RemoveAll(this);
		}
	}
}
#endif