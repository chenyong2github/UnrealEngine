// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "Misc/Base64.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/CoreRedirects.h"
#include "Engine/World.h"
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

	auto GetActorDescriptor = [this](const FAssetData& InAssetData) -> TUniquePtr<FWorldPartitionActorDesc>
	{
		FString ActorMetaDataClass;
		static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
		if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorMetaDataClass))
		{
			FString ActorMetaDataStr;
			static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
			if (InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr))
			{
				FString ActorClassName;
				FString ActorPackageName;
				if (!ActorMetaDataClass.Split(TEXT("."), &ActorPackageName, &ActorClassName))
				{
					ActorClassName = *ActorMetaDataClass;
				}

				// Look for a class redirectors
				FCoreRedirectObjectName OldClassName = FCoreRedirectObjectName(*ActorClassName, NAME_None, *ActorPackageName);
				FCoreRedirectObjectName NewClassName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldClassName);

				bool bIsValidClass = true;
				UClass* ActorClass = FindObject<UClass>(ANY_PACKAGE, *NewClassName.ToString(), true);

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

				TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::StaticCreateClassActorDesc(ActorDescInitData.NativeClass));

				NewActorDesc->Init(ActorDescInitData);
			
				if (!bIsValidClass)
				{
					UE_LOG(LogWorldPartition, Warning, TEXT("Invalid class `%s` for actor guid `%s` ('%s') from package '%s'"), *NewClassName.ToString(), *NewActorDesc->GetGuid().ToString(), *NewActorDesc->GetActorName().ToString(), *NewActorDesc->GetActorPackage().ToString());
					return nullptr;
				}

				return NewActorDesc;
			}
		}

		return nullptr;
	};

	for (const FAssetData& Asset : Assets)
	{
		TUniquePtr<FWorldPartitionActorDesc> ActorDesc = GetActorDescriptor(Asset);

		if (ActorDesc.IsValid())
		{
			AddActorDescriptor(ActorDesc.Release());
		}
	}

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
					// Pin the actor handle on the actor to prevent unloading it when unhashing
					FWorldPartitionHandle ExistingActorHandle(ExistingActorDesc);
					FWorldPartitionHandlePinRefScope ExistingActorHandlePin(ExistingActorHandle);

					TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(Actor->CreateActorDesc());

					OnActorDescUpdating(ExistingActorDesc->Get());

					// Transfer any internal values not coming from the actor
					NewActorDesc->TransferFrom(ExistingActorDesc->Get());

					*ExistingActorDesc = MoveTemp(NewActorDesc);

					OnActorDescUpdated(ExistingActorDesc->Get());
				}
				// New actor
				else
				{
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
	for (auto Iter = OldToNewObjectMap.CreateConstIterator(); Iter; ++Iter)
	{
		if (AActor* OldActor = Cast<AActor>(Iter->Key))
		{
			if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(OldActor->GetActorGuid()))
			{
				if (ActorDesc->GetActor() == OldActor)
				{
					ActorDesc->ActorPtr = Cast<AActor>(Iter->Value);
				}
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
	OutReferences.Reserve(OutReferences.Num() + GetActorDescCount());
	for (FActorDescList::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;
		OutReferences.Emplace(this, ActorDesc->GetGuid());
	}
}

void UActorDescContainer::RegisterEditorDelegates()
{
	if (GEditor && !IsTemplate() && World && !World->IsGameWorld())
	{
		FWorldDelegates::OnPostWorldRename.AddUObject(this, &UActorDescContainer::OnWorldRenamed);
		FCoreUObjectDelegates::OnObjectPreSave.AddUObject(this, &UActorDescContainer::OnObjectPreSave);
		FEditorDelegates::OnPackageDeleted.AddUObject(this, &UActorDescContainer::OnPackageDeleted);
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UActorDescContainer::OnObjectsReplaced);
	}
}

void UActorDescContainer::UnregisterEditorDelegates()
{
	if (GEditor && !IsTemplate() && World && !World->IsGameWorld())
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
