// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#include "UObject/ObjectSaveContext.h"
#include "Engine/World.h"

UActorDescContainer::FActorDescContainerInitializeDelegate UActorDescContainer::OnActorDescContainerInitialized;
#endif

UActorDescContainer::UActorDescContainer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
#if WITH_EDITOR
	, bContainerInitialized(false)
#endif
{}

void UActorDescContainer::Initialize(UWorld* InWorld, FName InPackageName)
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
		AssetRegistry.ScanPathsSynchronous({ LevelExternalActorsPath }, /*bForceRescan*/false, /*bIgnoreDenyListScanFilters*/false);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.PackagePaths.Add(*LevelExternalActorsPath);

		AssetRegistry.GetAssets(Filter, Assets);
	}

	for (const FAssetData& Asset : Assets)
	{
		TUniquePtr<FWorldPartitionActorDesc> ActorDesc = FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(Asset);

		if (ActorDesc.IsValid())
		{
			AddActorDescriptor(ActorDesc.Release());
		}
	}

	OnActorDescContainerInitialized.Broadcast(this);

	RegisterEditorDelegates();

	bContainerInitialized = true;
#endif
}

void UActorDescContainer::Uninitialize()
{
#if WITH_EDITOR
	if (bContainerInitialized)
	{
		UnregisterEditorDelegates();
		bContainerInitialized = false;
	}

	for (TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		if (FWorldPartitionActorDesc* ActorDesc = ActorDescPtr.Get())
		{
			RemoveActorDescriptor(ActorDesc);
		}
		ActorDescPtr.Reset();
	}
#endif
	World = nullptr;
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
void UActorDescContainer::AddActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	FActorDescList::AddActorDescriptor(ActorDesc);
	ActorDesc->SetContainer(this);
}

void UActorDescContainer::RemoveActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->SetContainer(nullptr);
	FActorDescList::RemoveActorDescriptor(ActorDesc);
}


void UActorDescContainer::OnWorldRenamed(UWorld* RenamedWorld)
{
	if (GetWorld() == RenamedWorld)
	{
		OnWorldRenamed();
	}
}

void UActorDescContainer::OnWorldRenamed()
{
	// Update container package
	ContainerPackageName = GetWorld()->GetPackage()->GetFName();
}

bool UActorDescContainer::ShouldHandleActorEvent(const AActor* Actor)
{
	return Actor && Actor->IsMainPackageActor() && (Actor->GetLevel() != nullptr) && (Actor->GetLevel()->GetPackage()->GetFName() == ContainerPackageName);
}

void UActorDescContainer::OnObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	if (!SaveContext.IsProceduralSave() && !(SaveContext.GetSaveFlags() & SAVE_FromAutosave))
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			if (ShouldHandleActorEvent(Actor))
			{
				check(IsValidChecked(Actor));
				if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = GetActorDescriptor(Actor->GetActorGuid()))
				{
					// Existing actor
					OnActorDescUpdating(ExistingActorDesc->Get());
					FWorldPartitionActorDescUtils::UpdateActorDescriptorFomActor(Actor, *ExistingActorDesc);
					OnActorDescUpdated(ExistingActorDesc->Get());
				}
				else
				{
					// New actor
					FWorldPartitionActorDesc* const AddedActorDesc = AddActor(Actor);
					OnActorDescAdded(AddedActorDesc);
				}
			}
		}
	}
}

void UActorDescContainer::OnPackageDeleted(UPackage* Package)
{
	AActor* Actor = AActor::FindActorInPackage(Package);

	if (ShouldHandleActorEvent(Actor))
	{
		RemoveActor(Actor->GetActorGuid());
	}
}

void UActorDescContainer::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewObjectMap)
{
	// Patch up Actor pointers in ActorDescs
	for (auto [OldObject, NewObject] : OldToNewObjectMap)
	{
		if (AActor* OldActor = Cast<AActor>(OldObject))
		{
			AActor* NewActor = CastChecked<AActor>(NewObject);
			if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(OldActor->GetActorGuid()))
			{				
				FWorldPartitionActorDescUtils::ReplaceActorDescriptorPointerFromActor(OldActor, NewActor, ActorDesc);
			}
		}
	}
}

void UActorDescContainer::RemoveActor(const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = GetActorDescriptor(ActorGuid))
	{
		OnActorDescRemoved(ExistingActorDesc->Get());
		RemoveActorDescriptor(ExistingActorDesc->Get());
		ExistingActorDesc->Reset();
	}
}

void UActorDescContainer::LoadAllActors(TArray<FWorldPartitionReference>& OutReferences)
{
	FWorldPartitionLoadingContext::FDeferred LoadingContext;
	OutReferences.Reserve(OutReferences.Num() + GetActorDescCount());
	for (FActorDescList::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;
		OutReferences.Emplace(this, ActorDesc->GetGuid());
	}
}

bool UActorDescContainer::ShouldRegisterDelegates()
{
	UWorld* OuterWorld = GetTypedOuter<UWorld>();
	// No need to register delegates for level instances
	bool bIsInstance = OuterWorld && OuterWorld->IsInstanced() && !OuterWorld->GetPackage()->HasAnyPackageFlags(PKG_NewlyCreated);
	return GEditor && !IsTemplate() && World && !World->IsGameWorld() && !bIsInstance;
}

void UActorDescContainer::RegisterEditorDelegates()
{
	if (ShouldRegisterDelegates())
	{
		FWorldDelegates::OnPostWorldRename.AddUObject(this, &UActorDescContainer::OnWorldRenamed);
		FCoreUObjectDelegates::OnObjectPreSave.AddUObject(this, &UActorDescContainer::OnObjectPreSave);
		FEditorDelegates::OnPackageDeleted.AddUObject(this, &UActorDescContainer::OnPackageDeleted);
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UActorDescContainer::OnObjectsReplaced);
	}
}

void UActorDescContainer::UnregisterEditorDelegates()
{
	if (ShouldRegisterDelegates())
	{
		FWorldDelegates::OnPostWorldRename.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectPreSave.RemoveAll(this);
		FEditorDelegates::OnPackageDeleted.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
	}
}

void UActorDescContainer::OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc)
{
	OnActorDescAddedEvent.Broadcast(NewActorDesc);
}

void UActorDescContainer::OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc)
{
	OnActorDescRemovedEvent.Broadcast(ActorDesc);
}
#endif
