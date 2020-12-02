// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceLevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "LevelInstancePrivate.h"
#include "LevelUtils.h"
#include "Misc/HashBuilder.h"

#if WITH_EDITOR
#include "LevelInstance/LevelInstanceEditorLevelStreaming.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ITransaction.h"
#include "AssetRegistryModule.h"
#include "AssetData.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "LevelInstance/ILevelInstanceEditorModule.h"
#include "HAL/PlatformTime.h"
#include "Engine/Selection.h"
#include "Engine/LevelBounds.h"
#include "LevelInstance/LevelInstanceEditorInstanceActor.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "Engine/Blueprint.h"
#include "LevelInstance/Packed/PackedLevelInstanceActor.h"
#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#endif

#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "LevelInstanceSubsystem"

DEFINE_LOG_CATEGORY(LogLevelInstance);

ULevelInstanceSubsystem::ULevelInstanceSubsystem()
	: UWorldSubsystem()
{

}

ULevelInstanceSubsystem::~ULevelInstanceSubsystem()
{

}

void ULevelInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	if (GEditor)
	{
		FModuleManager::LoadModuleChecked<ILevelInstanceEditorModule>("LevelInstanceEditor");
	}
#endif
}

void ULevelInstanceSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool ULevelInstanceSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return Super::DoesSupportWorldType(WorldType) || WorldType == EWorldType::EditorPreview;
}

ALevelInstance* ULevelInstanceSubsystem::GetLevelInstance(FLevelInstanceID LevelInstanceID) const
{
	if (ALevelInstance*const* LevelInstanceActor = RegisteredLevelInstances.Find(LevelInstanceID))
	{
		return *LevelInstanceActor;
	}

	return nullptr;
}

FLevelInstanceID ULevelInstanceSubsystem::ComputeLevelInstanceID(ALevelInstance* LevelInstanceActor) const
{
	FLevelInstanceID LevelInstanceID = InvalidLevelInstanceID;
	FHashBuilder HashBuilder;
	ForEachLevelInstanceAncestorsAndSelf(LevelInstanceActor, [&HashBuilder](const ALevelInstance* AncestorOrSelf)
	{
		HashBuilder << AncestorOrSelf->GetLevelInstanceActorGuid();
		return true;
	});
	LevelInstanceID = HashBuilder.GetHash();
	return LevelInstanceID;
}

FLevelInstanceID ULevelInstanceSubsystem::RegisterLevelInstance(ALevelInstance* LevelInstanceActor)
{
	FLevelInstanceID LevelInstanceID = ComputeLevelInstanceID(LevelInstanceActor);
	check(LevelInstanceID != InvalidLevelInstanceID);
	ALevelInstance*& Value = RegisteredLevelInstances.FindOrAdd(LevelInstanceID);
	check(GIsReinstancing || Value == nullptr || Value == LevelInstanceActor);
	Value = LevelInstanceActor;
	return LevelInstanceID;
}

void ULevelInstanceSubsystem::UnregisterLevelInstance(ALevelInstance* LevelInstanceActor)
{
	RegisteredLevelInstances.Remove(LevelInstanceActor->GetLevelInstanceID());
}

void ULevelInstanceSubsystem::RequestLoadLevelInstance(ALevelInstance* LevelInstanceActor, bool bForce /* = false */)
{
	check(LevelInstanceActor && !LevelInstanceActor->IsPendingKillOrUnreachable());
	if (LevelInstanceActor->IsLevelInstancePathValid())
	{
#if WITH_EDITOR
		if (!IsEditingLevelInstance(LevelInstanceActor))
#endif
		{
			LevelInstancesToUnload.Remove(LevelInstanceActor->GetLevelInstanceID());

			bool* bForcePtr = LevelInstancesToLoadOrUpdate.Find(LevelInstanceActor);

			// Avoid loading if already loaded. Can happen if actor requests unload/load in same frame. Without the force it means its not necessary.
			if (IsLoaded(LevelInstanceActor) && !bForce && (bForcePtr == nullptr || !(*bForcePtr)))
			{
				return;
			}

			if (bForcePtr != nullptr)
			{
				*bForcePtr |= bForce;
			}
			else
			{
				LevelInstancesToLoadOrUpdate.Add(LevelInstanceActor, bForce);
			}
		}
	}
}

void ULevelInstanceSubsystem::RequestUnloadLevelInstance(ALevelInstance* LevelInstanceActor)
{
	const FLevelInstanceID& LevelInstanceID = LevelInstanceActor->GetLevelInstanceID();
	if (LevelInstances.Contains(LevelInstanceID))
	{
		// LevelInstancesToUnload uses FLevelInstanceID because LevelInstanceActor* can be destroyed in later Tick and we don't need it.
		LevelInstancesToUnload.Add(LevelInstanceID);
	}
	LevelInstancesToLoadOrUpdate.Remove(LevelInstanceActor);
}

bool ULevelInstanceSubsystem::IsLoaded(const ALevelInstance* LevelInstanceActor) const
{
	return LevelInstanceActor->HasValidLevelInstanceID() && LevelInstances.Contains(LevelInstanceActor->GetLevelInstanceID());
}

void ULevelInstanceSubsystem::UpdateStreamingState()
{
	if (!LevelInstancesToUnload.Num() && !LevelInstancesToLoadOrUpdate.Num())
	{
		return;
	}

#if WITH_EDITOR
	// Do not update during transaction
	if (GUndo)
	{
		return;
	}

	FScopedSlowTask SlowTask(LevelInstancesToUnload.Num() + LevelInstancesToLoadOrUpdate.Num() * 2, LOCTEXT("UpdatingLevelInstances", "Updating Level Instances..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	check(!LevelsToRemoveScope);
	LevelsToRemoveScope.Reset(new FLevelsToRemoveScope());
#endif

	if (LevelInstancesToUnload.Num())
	{
		TSet<FLevelInstanceID> LevelInstancesToUnloadCopy(MoveTemp(LevelInstancesToUnload));
		for (const FLevelInstanceID& LevelInstanceID : LevelInstancesToUnloadCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("UnloadingLevelInstance", "Unloading Level Instance"));
#endif
			UnloadLevelInstance(LevelInstanceID);
		}
	}

	if (LevelInstancesToLoadOrUpdate.Num())
	{
		// Unload levels before doing any loading
		TMap<ALevelInstance*, bool> LevelInstancesToLoadOrUpdateCopy(MoveTemp(LevelInstancesToLoadOrUpdate));
		for (const TPair<ALevelInstance*, bool>& Pair : LevelInstancesToLoadOrUpdateCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("UnloadingLevelInstance", "Unloading Level Instance"));
#endif
			ALevelInstance* LevelInstanceActor = Pair.Key;
			if (Pair.Value)
			{
				UnloadLevelInstance(LevelInstanceActor->GetLevelInstanceID());
			}
		}

#if WITH_EDITOR
		LevelsToRemoveScope.Reset();
		double StartTime = FPlatformTime::Seconds();
#endif
		for (const TPair<ALevelInstance*, bool>& Pair : LevelInstancesToLoadOrUpdateCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("LoadingLevelInstance", "Loading Level Instance"));
#endif
			LoadLevelInstance(Pair.Key);
		}
#if WITH_EDITOR
		double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogLevelInstance, Log, TEXT("Loaded %s levels in %s seconds"), *FText::AsNumber(LevelInstancesToLoadOrUpdateCopy.Num()).ToString(), *FText::AsNumber(ElapsedTime).ToString());
#endif
	}

#if WITH_EDITOR
	LevelsToRemoveScope.Reset();
#endif
}

void ULevelInstanceSubsystem::LoadLevelInstance(ALevelInstance* LevelInstanceActor)
{
	check(LevelInstanceActor);
	if (IsLoaded(LevelInstanceActor) || LevelInstanceActor->IsPendingKillOrUnreachable() || !LevelInstanceActor->IsLevelInstancePathValid())
	{
		return;
	}

	const FLevelInstanceID& LevelInstanceID = LevelInstanceActor->GetLevelInstanceID();
	check(!LevelInstances.Contains(LevelInstanceID));

	if (ULevelStreamingLevelInstance* LevelStreaming = ULevelStreamingLevelInstance::LoadInstance(LevelInstanceActor))
	{
		FLevelInstance& LevelInstance = LevelInstances.Add(LevelInstanceID);
		LevelInstance.LevelStreaming = LevelStreaming;
#if WITH_EDITOR
		LevelInstanceActor->OnLevelInstanceLoaded();
#endif
	}
}

void ULevelInstanceSubsystem::UnloadLevelInstance(const FLevelInstanceID& LevelInstanceID)
{
#if WITH_EDITOR
	// Create scope if it doesn't exist
	bool bReleaseScope = false;
	if (!LevelsToRemoveScope)
	{
		bReleaseScope = true;
		LevelsToRemoveScope.Reset(new FLevelsToRemoveScope());
	}
#endif
				
	FLevelInstance LevelInstance;
	if (LevelInstances.RemoveAndCopyValue(LevelInstanceID, LevelInstance))
	{
		if (ULevel* LoadedLevel = LevelInstance.LevelStreaming->GetLoadedLevel())
		{
			ForEachActorInLevel(LoadedLevel, [this](AActor* LevelActor)
			{
				if (ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(LevelActor))
				{
					// Make sure to remove from pending loads if we are unloading child can't be loaded
					LevelInstancesToLoadOrUpdate.Remove(LevelInstanceActor);
					
					UnloadLevelInstance(LevelInstanceActor->GetLevelInstanceID());
				}
				return true;
			});
		}

		ULevelStreamingLevelInstance::UnloadInstance(LevelInstance.LevelStreaming);
	}

#if WITH_EDITOR
	if (bReleaseScope)
	{
		LevelsToRemoveScope.Reset();
	}
#endif
}

void ULevelInstanceSubsystem::ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const
{
	for (AActor* LevelActor : Level->Actors)
	{
		if (LevelActor && !LevelActor->IsPendingKill())
		{
			if (!Operation(LevelActor))
			{
				return;
			}
		}
	}
}

void ULevelInstanceSubsystem::ForEachLevelInstanceAncestorsAndSelf(AActor* Actor, TFunctionRef<bool(ALevelInstance*)> Operation) const
{
	if (ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
	{
		if (!Operation(LevelInstanceActor))
		{
			return;
		}
	}

	ForEachLevelInstanceAncestors(Actor, Operation);
}

void ULevelInstanceSubsystem::ForEachLevelInstanceAncestors(AActor* Actor, TFunctionRef<bool(ALevelInstance*)> Operation) const
{
	ALevelInstance* ParentLevelInstance = nullptr;
	do
	{
		ParentLevelInstance = GetOwningLevelInstance(Actor->GetLevel());
		Actor = ParentLevelInstance;

	} while (ParentLevelInstance != nullptr && Operation(ParentLevelInstance));
}

ALevelInstance* ULevelInstanceSubsystem::GetOwningLevelInstance(const ULevel* Level) const
{
	if (ULevelStreaming* BaseLevelStreaming = FLevelUtils::FindStreamingLevel(Level))
	{
#if WITH_EDITOR
		if (ULevelStreamingLevelInstanceEditor* LevelStreamingEditor = Cast<ULevelStreamingLevelInstanceEditor>(BaseLevelStreaming))
		{
			return LevelStreamingEditor->GetLevelInstanceActor();
		}
		else 
#endif
		if (ULevelStreamingLevelInstance* LevelStreaming = Cast<ULevelStreamingLevelInstance>(BaseLevelStreaming))
		{
			return LevelStreaming->GetLevelInstanceActor();
		}
		else if (UWorldPartitionLevelStreamingDynamic* WorldPartitionLevelStreaming = Cast<UWorldPartitionLevelStreamingDynamic>(BaseLevelStreaming))
		{
			return GetOwningLevelInstance(WorldPartitionLevelStreaming->GetOuterWorld()->PersistentLevel);
		}
	}

	return nullptr;
}

#if WITH_EDITOR

void ULevelInstanceSubsystem::Tick()
{
	// For non-game world, Tick is responsible of processing LevelInstances to update/load/unload
	if (!GetWorld()->IsGameWorld())
	{
		UpdateStreamingState();

		// Begin editing the pending LevelInstance when loads complete
		if (PendingLevelInstanceToEdit != InvalidLevelInstanceID && !LevelInstancesToLoadOrUpdate.Num())
		{
			if (ALevelInstance** LevelInstanceActor = RegisteredLevelInstances.Find(PendingLevelInstanceToEdit))
			{
				EditLevelInstance(*LevelInstanceActor);
			}
		}
	}
}

bool ULevelInstanceSubsystem::CanPackLevelInstances() const
{
	return !LevelInstanceEdits.Num();
}

void ULevelInstanceSubsystem::PackLevelInstances()
{
	if (!CanPackLevelInstances())
	{
		return;
	}

	TArray<APackedLevelInstance*> PackedLevelInstancesToUpdate;
	TSet<UBlueprint*> BlueprintsToUpdate;
	for (TObjectIterator<UWorld> It(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill); It; ++It)
	{
		UWorld* CurrentWorld = *It;
		for (TActorIterator<ALevelInstance> LevelInstanceIt(GetWorld()); LevelInstanceIt; ++LevelInstanceIt)
		{
			if (APackedLevelInstance* PackedLevelInstance = Cast<APackedLevelInstance>(*LevelInstanceIt))
			{
				if (UBlueprint* Blueprint = Cast<UBlueprint>(PackedLevelInstance->GetClass()->ClassGeneratedBy))
				{
					BlueprintsToUpdate.Add(Blueprint);
				}
				else
				{
					PackedLevelInstancesToUpdate.Add(PackedLevelInstance);
				}
			}
		}
	}

	int32 Count = BlueprintsToUpdate.Num() + PackedLevelInstancesToUpdate.Num();
	if (!Count)
	{
		return;
	}
		
	FScopedSlowTask SlowTask(Count, (LOCTEXT("LevelInstance_PackLevelInstances", "Packing Level Instances")));
	SlowTask.MakeDialog();
		
	auto UpdateProgress = [&SlowTask]()
	{
		if (SlowTask.CompletedWork < SlowTask.TotalAmountOfWork)
		{
			SlowTask.EnterProgressFrame(1, FText::Format(LOCTEXT("LevelInstance_PackLevelInstancesProgress", "Packing Level Instance {0} of {1})"), FText::AsNumber(SlowTask.CompletedWork), FText::AsNumber(SlowTask.TotalAmountOfWork)));
		}
	};

	for (APackedLevelInstance* PackedLevelInstance : PackedLevelInstancesToUpdate)
	{
		PackedLevelInstance->OnWorldAssetChanged();
		UpdateProgress();
	}

	TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
	for (UBlueprint* Blueprint : BlueprintsToUpdate)
	{
		Builder->UpdateBlueprint(Blueprint);
		UpdateProgress();
	}
}

bool ULevelInstanceSubsystem::GetLevelInstanceBounds(const ALevelInstance* LevelInstanceActor, FBox& OutBounds) const
{
	if (IsLoaded(LevelInstanceActor))
	{
		const FLevelInstance& LevelInstance = LevelInstances.FindChecked(LevelInstanceActor->GetLevelInstanceID());
		OutBounds = LevelInstance.LevelStreaming->GetBounds();
		return true;
	}
	else if(LevelInstanceActor->IsLevelInstancePathValid())
	{
		if (ULevel::GetLevelBoundsFromPackage(FName(*LevelInstanceActor->GetWorldAssetPackage()), OutBounds))
		{
			return true;
		}
	}

	return false;
}

void ULevelInstanceSubsystem::ForEachActorInLevelInstance(const ALevelInstance* LevelInstanceActor, TFunctionRef<bool(AActor * LevelActor)> Operation) const
{
	if (ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor))
	{
		ForEachActorInLevel(LevelInstanceLevel, Operation);
	}
}

void ULevelInstanceSubsystem::ForEachLevelInstanceAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const
{
	if (const ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
	{
		if (!Operation(LevelInstanceActor))
		{
			return;
		}
	}

	ForEachLevelInstanceAncestors(Actor, Operation);
}

void ULevelInstanceSubsystem::ForEachLevelInstanceAncestors(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const
{
	const ALevelInstance* ParentLevelInstance = nullptr;
	do 
	{
		ParentLevelInstance = GetOwningLevelInstance(Actor->GetLevel());
		Actor = ParentLevelInstance;
	} 
	while (ParentLevelInstance != nullptr && Operation(ParentLevelInstance));
}

void ULevelInstanceSubsystem::ForEachLevelInstanceChildren(const ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(const ALevelInstance*)> Operation) const
{
	if (ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor))
	{
		ForEachActorInLevel(LevelInstanceLevel, [this, Operation,bRecursive](AActor* LevelActor)
		{
			if (const ALevelInstance* ChildLevelInstanceActor = Cast<ALevelInstance>(LevelActor))
			{
				if (Operation(ChildLevelInstanceActor) && bRecursive)
				{
					ForEachLevelInstanceChildren(ChildLevelInstanceActor, bRecursive, Operation);
				}
			}
			return true;
		});
	}
}

void ULevelInstanceSubsystem::ForEachLevelInstanceChildren(ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(ALevelInstance*)> Operation) const
{
	if (ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor))
	{
		ForEachActorInLevel(LevelInstanceLevel, [this, Operation, bRecursive](AActor* LevelActor)
		{
			if (ALevelInstance* ChildLevelInstanceActor = Cast<ALevelInstance>(LevelActor))
			{
				if (Operation(ChildLevelInstanceActor) && bRecursive)
				{
					ForEachLevelInstanceChildren(ChildLevelInstanceActor, bRecursive, Operation);
				}
			}
			return true;
		});
	}
}

void ULevelInstanceSubsystem::ForEachLevelInstanceEdit(TFunctionRef<bool(ALevelInstance*)> Operation) const
{
	for (const auto& Pair : LevelInstanceEdits)
	{
		if (!Operation(Pair.Value.LevelStreaming->GetLevelInstanceActor()))
		{
			return;
		}
	}
}

bool ULevelInstanceSubsystem::HasDirtyChildrenLevelInstances(const ALevelInstance* LevelInstanceActor) const
{
	bool bDirtyChildren = false;
	ForEachLevelInstanceChildren(LevelInstanceActor, /*bRecursive=*/true, [this, &bDirtyChildren](const ALevelInstance* ChildLevelInstanceActor)
	{
		if (IsEditingLevelInstanceDirty(ChildLevelInstanceActor))
		{
			bDirtyChildren = true;
			return false;
		}
		return true;
	});
	return bDirtyChildren;
}

bool ULevelInstanceSubsystem::HasEditingChildrenLevelInstances(const ALevelInstance* LevelInstanceActor) const
{
	bool bEditingChildren = false;
	ForEachLevelInstanceChildren(LevelInstanceActor, true, [this, &bEditingChildren](const ALevelInstance* ChildLevelInstanceActor)
		{
			if (IsEditingLevelInstance(ChildLevelInstanceActor))
			{
				bEditingChildren = true;
				return false;
			}
			return true;
		});
	return bEditingChildren;
}

void ULevelInstanceSubsystem::SetIsTemporarilyHiddenInEditor(ALevelInstance* LevelInstanceActor, bool bIsHidden)
{
	if (ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor))
	{
		ForEachActorInLevel(LevelInstanceLevel, [bIsHidden](AActor* LevelActor)
		{
			LevelActor->SetIsTemporarilyHiddenInEditor(bIsHidden);
			return true;
		});
	}
}

bool ULevelInstanceSubsystem::SetCurrent(ALevelInstance* LevelInstanceActor) const
{
	if (IsEditingLevelInstance(LevelInstanceActor))
	{
		return GetWorld()->SetCurrentLevel(GetLevelInstanceLevel(LevelInstanceActor));
	}

	return false;
}

bool ULevelInstanceSubsystem::IsCurrent(const ALevelInstance* LevelInstanceActor) const
{
	if (IsEditingLevelInstance(LevelInstanceActor))
	{
		return GetLevelInstanceLevel(LevelInstanceActor) == GetWorld()->GetCurrentLevel();
	}

	return false;
}

bool ULevelInstanceSubsystem::MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel) const
{
	check(DestinationLevel);

	const bool bWarnAboutReferences = true;
	const bool bWarnAboutRenaming = true;
	const bool bMoveAllOrFail = true;
	if (!EditorLevelUtils::MoveActorsToLevel(ActorsToRemove, DestinationLevel, bWarnAboutReferences, bWarnAboutRenaming, bMoveAllOrFail))
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("Failed to move actors out of Level Instance because not all actors could be moved"));
		return false;
	}

	ALevelInstance* OwningInstance = GetOwningLevelInstance(DestinationLevel);
	if (!OwningInstance || !OwningInstance->IsEditing())
	{
		for (const auto& Actor : ActorsToRemove)
		{
			const bool bEditing = false;
			Actor->PushLevelInstanceEditingStateToProxies(bEditing);
		}
	}

	return true;
}

bool ULevelInstanceSubsystem::MoveActorsTo(ALevelInstance* LevelInstanceActor, const TArray<AActor*>& ActorsToMove)
{
	check(IsEditingLevelInstance(LevelInstanceActor));
	ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor);
	check(LevelInstanceLevel);

	return MoveActorsToLevel(ActorsToMove, LevelInstanceLevel);
}

ALevelInstance* ULevelInstanceSubsystem::CreateLevelInstanceFrom(const TArray<AActor*>& ActorsToMove, ELevelInstanceCreationType CreationType, UWorld* TemplateWorld)
{
	ULevel* CurrentLevel = GetWorld()->GetCurrentLevel();
		
	if (ActorsToMove.Num() == 0)
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("Failed to create Level Instance from empty actor array"));
		return nullptr;
	}
		
	FBox ActorLocationBox(ForceInit);
	for (const AActor* ActorToMove : ActorsToMove)
	{
		const bool bNonColliding = false;
		const bool bIncludeChildren = true;
		ActorLocationBox += ActorToMove->GetComponentsBoundingBox(bNonColliding, bIncludeChildren);

		FText Reason;
		if (!CanMoveActorToLevel(ActorToMove, &Reason))
		{
			UE_LOG(LogLevelInstance, Warning, TEXT("%s"), *Reason.ToString());
			return nullptr;
		}
	}

	FVector LevelInstanceLocation = ActorLocationBox.GetCenter();
	LevelInstanceLocation.Z = ActorLocationBox.Min.Z;

	ULevelStreamingLevelInstanceEditor* LevelStreaming = StaticCast<ULevelStreamingLevelInstanceEditor*>(EditorLevelUtils::CreateNewStreamingLevelForWorld(*GetWorld(), ULevelStreamingLevelInstanceEditor::StaticClass(), TEXT(""), false, TemplateWorld));
	if (!LevelStreaming)
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("Failed to create new Level Instance level"));
		return nullptr;
	}

	ULevel* LoadedLevel = LevelStreaming->GetLoadedLevel();
	check(LoadedLevel);

	const bool bWarnAboutReferences = true;
	const bool bWarnAboutRenaming = true;
	const bool bMoveAllOrFail = true;

	if (!EditorLevelUtils::MoveActorsToLevel(ActorsToMove, LoadedLevel, bWarnAboutReferences, bWarnAboutRenaming, bMoveAllOrFail))
	{
		ULevelStreamingLevelInstanceEditor::Unload(LevelStreaming);
		UE_LOG(LogLevelInstance, Warning, TEXT("Failed to create Level Instance because some actors couldn't be moved"));
		return nullptr;
	}
	
	// Take all actors out of any folders they may have been in since we don't support folders inside of level instances
	for (AActor* Actor : LoadedLevel->Actors)
	{
		if (Actor)
		{
			Actor->SetFolderPath_Recursively(NAME_None);
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = CurrentLevel;
	ALevelInstance* NewLevelInstanceActor = nullptr;
	TSoftObjectPtr<UWorld> WorldPtr(LoadedLevel->GetTypedOuter<UWorld>());
	
	if (CreationType == ELevelInstanceCreationType::LevelInstance)
	{
		NewLevelInstanceActor = GetWorld()->SpawnActor<ALevelInstance>(ALevelInstance::StaticClass(), SpawnParams);
	}
	else if (CreationType == ELevelInstanceCreationType::PackedLevelInstance)
	{
		NewLevelInstanceActor = GetWorld()->SpawnActor<APackedLevelInstance>(APackedLevelInstance::StaticClass(), SpawnParams);
	}
	else if (CreationType == ELevelInstanceCreationType::PackedLevelInstanceBlueprint)
	{
		int32 LastSlashIndex = 0;
		FString LongPackageName = WorldPtr.GetLongPackageName();
		LongPackageName.FindLastChar('/', LastSlashIndex);

		FString PackagePath = LongPackageName.Mid(0, LastSlashIndex == INDEX_NONE ? MAX_int32 : LastSlashIndex);
		FString AssetName = WorldPtr.GetAssetName() + FPackedLevelInstanceBuilder::GetPackedBPSuffix();
		const bool bCompile = true;
		if (UBlueprint* NewBP = FPackedLevelInstanceBuilder::CreatePackedLevelInstanceBlueprint(PackagePath, AssetName, bCompile))
		{
			NewLevelInstanceActor = GetWorld()->SpawnActor<APackedLevelInstance>(NewBP->GeneratedClass, SpawnParams);
			APackedLevelInstance* CDO = Cast<APackedLevelInstance>(NewBP->GeneratedClass->GetDefaultObject());
			CDO->SetWorldAsset(WorldPtr);
		}

		if (!NewLevelInstanceActor)
		{
			UE_LOG(LogLevelInstance, Warning, TEXT("Failed to create packed level blueprint. Creating non blueprint packed level instance instead."));
			NewLevelInstanceActor = GetWorld()->SpawnActor<APackedLevelInstance>(APackedLevelInstance::StaticClass(), SpawnParams);
		}
	}
	
	check(NewLevelInstanceActor);
	NewLevelInstanceActor->SetWorldAsset(WorldPtr);
	NewLevelInstanceActor->SetActorLocation(LevelInstanceLocation);

	// Actors were moved and kept their World positions so when saving we want their positions to actually be relative to the FounationActor/LevelTransform
	// so we set the LevelTransform and we mark the level as having moved its actors. 
	// On Level save FLevelUtils::RemoveEditorTransform will fixup actor transforms to make them relative to the LevelTransform.
	LevelStreaming->LevelTransform = NewLevelInstanceActor->GetActorTransform();
	LoadedLevel->bAlreadyMovedActors = true;

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(NewLevelInstanceActor, true, true);

	NewLevelInstanceActor->OnEdit();

	FLevelInstanceEdit& LevelInstanceEdit = LevelInstanceEdits.Add(FName(*NewLevelInstanceActor->GetWorldAssetPackage()));
	LevelInstanceEdit.LevelStreaming = LevelStreaming;
	LevelStreaming->LevelInstanceID = NewLevelInstanceActor->GetLevelInstanceID();
		
	GetWorld()->SetCurrentLevel(LoadedLevel);
				
	// Use LevelStreaming->GetLevelInstanceActor() because OnWorldAssetSaved could've reinstanced the LevelInstanceActor
	return CommitLevelInstance(LevelStreaming->GetLevelInstanceActor());
}

bool ULevelInstanceSubsystem::BreakLevelInstance(ALevelInstance* LevelInstanceActor, uint32 Levels)
{
	if (Levels > 0)
	{
		// Can only break the top level LevelInstance
		check(LevelInstanceActor->GetLevel() == GetWorld()->GetCurrentLevel());

		// need to ensure that LevelInstanceActor has been streamed in fully
		GEngine->BlockTillLevelStreamingCompleted(LevelInstanceActor->GetWorld());

		TArray<AActor*> ActorsToMove;
		ForEachActorInLevelInstance(LevelInstanceActor, [this, &ActorsToMove](AActor* Actor)
			{
				// Skip some actor types
				if (!Actor->IsA<ALevelBounds>() && !Actor->IsA<ABrush>() && !Actor->IsA<AWorldSettings>() && !Actor->IsA<ALevelInstanceEditorInstanceActor>())
				{
					if (CanMoveActorToLevel(Actor))
					{
						FSetActorHiddenInSceneOutliner Show(Actor, false);
						Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
						ActorsToMove.Add(Actor);
					}
				}

				return true;
			});

		ULevel* DestinationLevel = GetWorld()->GetCurrentLevel();
		check(DestinationLevel);

		const bool bWarnAboutReferences = true;
		const bool bWarnAboutRenaming = true;
		const bool bMoveAllOrFail = true;
		if (!EditorLevelUtils::MoveActorsToLevel(ActorsToMove, DestinationLevel, bWarnAboutReferences, bWarnAboutRenaming, bMoveAllOrFail))
		{
			UE_LOG(LogLevelInstance, Warning, TEXT("Failed to break Level Instance because not all actors could be moved"));
			return false;
		}

		// Destroy the old LevelInstance instance actor
		GetWorld()->DestroyActor(LevelInstanceActor);
	
		// Break up any sub LevelInstances if more levels are requested
		if (Levels > 1)
		{
			TArray<ALevelInstance*> Children;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				AActor* Actor = Cast<AActor>(*It);
				if (ALevelInstance* ChildLevelInstance = Cast<ALevelInstance>(Actor))
				{
					Children.Add(ChildLevelInstance);
				}
			}

			for (auto& Child : Children)
			{
				BreakLevelInstance(Child, Levels - 1);
			}
		}
	}

	return true;
}

ULevel* ULevelInstanceSubsystem::GetLevelInstanceLevel(const ALevelInstance* LevelInstanceActor) const
{
	if (LevelInstanceActor->HasValidLevelInstanceID())
	{
		if (const FLevelInstanceEdit* LevelInstanceEdit = GetLevelInstanceEdit(LevelInstanceActor))
		{
			return LevelInstanceEdit->LevelStreaming->GetLoadedLevel();
		}
		else if (const FLevelInstance* LevelInstance = LevelInstances.Find(LevelInstanceActor->GetLevelInstanceID()))
		{
			return LevelInstance->LevelStreaming->GetLoadedLevel();
		}
	}

	return nullptr;
}

void ULevelInstanceSubsystem::RemoveLevelFromWorld(ULevel* Level, bool bResetTrans)
{
	if (LevelsToRemoveScope)
	{
		LevelsToRemoveScope->Levels.AddUnique(Level);
		LevelsToRemoveScope->bResetTrans |= bResetTrans;
	}
	else
	{
		EditorLevelUtils::RemoveLevelFromWorld(Level, false, bResetTrans);
	}
}

ULevelInstanceSubsystem::FLevelsToRemoveScope::~FLevelsToRemoveScope()
{
	if (Levels.Num() > 0)
	{
		double StartTime = FPlatformTime::Seconds();
		const bool bClearSelection = false;
		// No need to clear the whole editor selection since actor of this level will be removed from the selection by: UEditorEngine::OnLevelRemovedFromWorld
		EditorLevelUtils::RemoveLevelsFromWorld(Levels, bClearSelection, bResetTrans);
		double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogLevelInstance, Log, TEXT("Unloaded %s levels in %s seconds"), *FText::AsNumber(Levels.Num()).ToString(), *FText::AsNumber(ElapsedTime).ToString());
	}
}

bool ULevelInstanceSubsystem::CanMoveActorToLevel(const AActor* Actor, FText* OutReason) const
{
	if (Actor->GetWorld() == GetWorld())
	{
		if (const ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
		{
			if (IsEditingLevelInstance(LevelInstanceActor))
			{
				if (OutReason != nullptr)
				{
					*OutReason = LOCTEXT("CanMoveActorLevelEditing", "Can't move Level Instance actor while it is being edited");
				}
				return false;
			}

			bool bEditingChildren = false;
			ForEachLevelInstanceChildren(LevelInstanceActor, true, [this, &bEditingChildren](const ALevelInstance* ChildLevelInstanceActor)
			{
				if (IsEditingLevelInstance(ChildLevelInstanceActor))
				{
					bEditingChildren = true;
					return false;
				}
				return true;
			});

			if (bEditingChildren)
			{
				if (OutReason != nullptr)
				{
					*OutReason = LOCTEXT("CanMoveActorToLevelChildEditing", "Can't move Level Instance actor while one of its child Level Instance is being edited");
				}
				return false;
			}
		}
	}

	return true;
}

void ULevelInstanceSubsystem::DiscardEdits()
{
	for (const auto& Pair : LevelInstanceEdits)
	{
		ULevelStreamingLevelInstanceEditor::Unload(Pair.Value.LevelStreaming);
	}
	LevelInstanceEdits.Empty();
}

void ULevelInstanceSubsystem::OnActorDeleted(AActor* Actor)
{
	if (ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
	{
		if (Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			// We are receiving this event when destroying the old actor after BP reinstantiation. In this case,
			// the newly created actor was already added to the list, so we can safely ignore this case.
			check(GIsReinstancing);
			return;
		}

		const bool bAlreadyRooted = LevelInstanceActor->IsRooted();
		// Unloading LevelInstances leads to GC and Actor can be collected. Add to root temp. It will get collected after the OnActorDeleted callbacks
		if (!bAlreadyRooted)
		{
			LevelInstanceActor->AddToRoot();
		}

		FScopedSlowTask SlowTask(0, LOCTEXT("UnloadingLevelInstances", "Unloading Level Instances..."), !GetWorld()->IsGameWorld());
		SlowTask.MakeDialog();
		check(!IsEditingLevelInstanceDirty(LevelInstanceActor) && !HasDirtyChildrenLevelInstances(LevelInstanceActor));
		if (IsEditingLevelInstance(LevelInstanceActor))
		{
			CommitLevelInstance(LevelInstanceActor);
		}
		CommitChildrenLevelInstances(LevelInstanceActor);
		LevelInstancesToLoadOrUpdate.Remove(LevelInstanceActor);
				
		UnloadLevelInstance(LevelInstanceActor->GetLevelInstanceID());
		
		// Remove from root so it gets collected on the next GC if it can be.
		if (!bAlreadyRooted)
		{
			LevelInstanceActor->RemoveFromRoot();
		}
	}
}

bool ULevelInstanceSubsystem::ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld)
{
	if (DirtyPackage == EditingWorld->GetOutermost())
	{
		return false;
	}

	bool bIgnore = true;
	ForEachObjectWithPackage(DirtyPackage, [&bIgnore, EditingWorld](UObject* Object)
	{
		if (Object->GetOutermostObject() == EditingWorld)
		{
			bIgnore = false;
		}

		return bIgnore;
	});

	return bIgnore;
}

UWorld* ULevelInstanceSubsystem::FLevelInstanceEdit::GetEditWorld() const
{
	if (LevelStreaming && LevelStreaming->GetLoadedLevel())
	{
		return LevelStreaming->GetLoadedLevel()->GetTypedOuter<UWorld>();
	}

	return nullptr;
}

const ULevelInstanceSubsystem::FLevelInstanceEdit* ULevelInstanceSubsystem::GetLevelInstanceEdit(const ALevelInstance* LevelInstanceActor) const
{
	const FLevelInstanceEdit* LevelInstanceEdit = LevelInstanceEdits.Find(FName(*LevelInstanceActor->GetWorldAssetPackage()));
	if (!LevelInstanceEdit || LevelInstanceEdit->LevelStreaming->GetLevelInstanceActor() != LevelInstanceActor)
	{
		return nullptr;
	}

	return LevelInstanceEdit;
}

bool ULevelInstanceSubsystem::IsEditingLevelInstanceDirty(const ALevelInstance* LevelInstanceActor) const
{
	const FLevelInstanceEdit* LevelInstanceEdit = GetLevelInstanceEdit(LevelInstanceActor);
	if (!LevelInstanceEdit)
	{
		return false;
	}

	return IsLevelInstanceEditDirty(LevelInstanceEdit);
}

bool ULevelInstanceSubsystem::IsLevelInstanceEditDirty(const FLevelInstanceEdit* LevelInstanceEdit) const
{
	const UWorld* EditingWorld = LevelInstanceEdit->GetEditWorld();
	check(EditingWorld);

	TArray<UPackage*> OutDirtyPackages;
	FEditorFileUtils::GetDirtyPackages(OutDirtyPackages, [EditingWorld](UPackage* DirtyPackage)
	{
		return ULevelInstanceSubsystem::ShouldIgnoreDirtyPackage(DirtyPackage, EditingWorld);
	});

	return OutDirtyPackages.Num() > 0;
}

bool ULevelInstanceSubsystem::CanEditLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason) const
{
	// Only allow Editing in Editor World
	if (GetWorld()->WorldType != EWorldType::Editor)
	{
		return false;
	}

	if (ULevel* LevelInstanceLevel = GetLevelInstanceLevel(LevelInstanceActor))
	{
		if (LevelInstanceLevel->GetWorldPartition())
		{
			if (OutReason)
			{
				*OutReason = LOCTEXT("CanEditPartitionedLevelInstance", "Can't edit partitioned Level Instance");
			}
			return false;
		}
	}

	if (const FLevelInstanceEdit* LevelInstanceEdit = LevelInstanceEdits.Find(FName(*LevelInstanceActor->GetWorldAssetPackage())))
	{
		if (OutReason)
		{
			if (LevelInstanceEdit->LevelStreaming->GetLevelInstanceActor() == LevelInstanceActor)
			{
				*OutReason = LOCTEXT("CanEditLevelInstanceAlreadyBeingEdited", "Level Instance already being edited");
			}
			else
			{

				*OutReason = LOCTEXT("CanEditLevelInstanceAlreadyEditing", "Another Level Instance pointing to the same level is being edited");
			}
		}
		return false;
	}

	// Do not allow multiple LevelInstances of the same hierarchy to be edited... (checking ancestors)
	bool bAncestorBeingEdited = false;
	if (LevelInstanceEdits.Num() > 0)
	{
		ForEachLevelInstanceAncestors(LevelInstanceActor, [this, &bAncestorBeingEdited, OutReason](const ALevelInstance* AncestorLevelInstance)
		{
			if (const FLevelInstanceEdit* LevelInstanceEdit = LevelInstanceEdits.Find(FName(*AncestorLevelInstance->GetWorldAssetPackage())))
			{
				// Allow children to be edited if ancestor is clean
				if (IsLevelInstanceEditDirty(LevelInstanceEdit))
				{
					if (OutReason)
					{
						*OutReason = LOCTEXT("CanEditLevelInstanceAncestorBeingEdited", "Ancestor Level Instance is being edited and dirty. Commit changes first.");
					}
					bAncestorBeingEdited = true;
					return false;
				}
			}
			return true;
		});
	}

	if (bAncestorBeingEdited)
	{
		return false;
	}
		
	// Do not allow multiple LevelInstances of the same hierarchy to be edited... (checking children)
	bool bChildBeingEdited = false;
	if (LevelInstanceEdits.Num() > 0)
	{
		for (const auto& Pair : LevelInstanceEdits)
		{
			check(Pair.Value.LevelStreaming);
			ALevelInstance* LevelInstanceEditActor = Pair.Value.LevelStreaming->GetLevelInstanceActor();
			check(LevelInstanceEditActor);
			ForEachLevelInstanceAncestors(LevelInstanceEditActor, [this, &bChildBeingEdited, OutReason, LevelInstanceActor](const ALevelInstance* AncestorLevelInstance)
			{
				if (AncestorLevelInstance->GetWorldAsset() == LevelInstanceActor->GetWorldAsset())
				{
					if (const FLevelInstanceEdit* AncestorLevelInstanceEdit = LevelInstanceEdits.Find(FName(*AncestorLevelInstance->GetWorldAssetPackage())))
					{
						if (IsLevelInstanceEditDirty(AncestorLevelInstanceEdit))
						{
							if (OutReason)
							{
								*OutReason = LOCTEXT("CanEditLevelInstanceOtherChildren", "Children Level Instance already being edited and dirty. Commit changes first.");
							}
							bChildBeingEdited = true;
							return false;
						}
					}
				}

				return true;
			});

			if (bChildBeingEdited)
			{
				break;
			}
		}
	}
	
	if (bChildBeingEdited)
	{
		return false;
	}

	if (!LevelInstanceActor->IsLevelInstancePathValid())
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanEditLevelInstanceDirtyInvalid", "Level Instance path is invalid");
		}
		return false;
	}

	if (FLevelUtils::FindStreamingLevel(GetWorld(), *LevelInstanceActor->GetWorldAssetPackage()))
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanEditLevelInstanceAlreadyExists", "The same level was added to world outside of Level Instances");
		}
		return false;
	}

	return true;
}

bool ULevelInstanceSubsystem::CanCommitLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason) const
{
	if (!IsEditingLevelInstance(LevelInstanceActor))
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanCommitLevelInstanceNotEditing", "Level Instance is not currently being edited");
		}
		return false;
	}

	return true;
}

void ULevelInstanceSubsystem::EditLevelInstance(ALevelInstance* LevelInstanceActor, TWeakObjectPtr<AActor> ContextActorPtr)
{
	check(CanEditLevelInstance(LevelInstanceActor));
	const bool bWasPendingEdit = PendingLevelInstanceToEdit != InvalidLevelInstanceID;
	PendingLevelInstanceToEdit = InvalidLevelInstanceID;
		
	FScopedSlowTask SlowTask(0, LOCTEXT("BeginEditLevelInstance", "Loading Level Instance for edit..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	// Gather information from the context actor to try and select something meaningful after the loading
	FString ActorNameToSelect;
	if (AActor* ContextActor = ContextActorPtr.Get())
	{
		ActorNameToSelect = ContextActor->GetName();
		ForEachLevelInstanceAncestorsAndSelf(ContextActor, [&ActorNameToSelect,LevelInstanceActor](const ALevelInstance* AncestorLevelInstanceActor)
		{
			// stop when we hit the LevelInstance we are about to edit
			if (AncestorLevelInstanceActor == LevelInstanceActor)
			{
				return false;
			}
			
			ActorNameToSelect = AncestorLevelInstanceActor->GetName();
			return true;
		});
	}

	GEditor->SelectNone(false, true);

	// Check if there is an open (but clean) ancestor and child and unload it before opening the LevelInstance for editing
	if (LevelInstanceEdits.Num() > 0)
	{
		ALevelInstance* LevelInstanceToCommit = nullptr;

		auto GetLevelInstanceToCommit = [this, &LevelInstanceToCommit](ALevelInstance* LevelInstance)
		{
			if (const FLevelInstanceEdit* LevelInstanceEdit = LevelInstanceEdits.Find(FName(*LevelInstance->GetWorldAssetPackage())))
			{
				check(!IsLevelInstanceEditDirty(LevelInstanceEdit));
				check(LevelInstanceToCommit == nullptr);
				LevelInstanceToCommit = LevelInstanceEdit->LevelStreaming->GetLevelInstanceActor();
				check(LevelInstanceToCommit != nullptr);
				return false;
			}
			return true;
		};

		ForEachLevelInstanceAncestors(LevelInstanceActor, [&LevelInstanceToCommit, &GetLevelInstanceToCommit](ALevelInstance* AncestorLevelInstance)
		{
			if (!LevelInstanceToCommit)
			{
				GetLevelInstanceToCommit(AncestorLevelInstance);
			}
			AncestorLevelInstance->OnEditChild();
			return true;
		});

		if (!LevelInstanceToCommit)
		{
			ForEachLevelInstanceChildren(LevelInstanceActor, true, GetLevelInstanceToCommit);
		}
		
		if (LevelInstanceToCommit)
		{
			PendingLevelInstanceToEdit = LevelInstanceActor->GetLevelInstanceID();
			CommitLevelInstance(LevelInstanceToCommit);

			// Stop here. The LevelInstance will be open for editing after an async reload.
			return;
		}
	}

	// Cleanup async requests in case
	LevelInstancesToUnload.Remove(LevelInstanceActor->GetLevelInstanceID());
	LevelInstancesToLoadOrUpdate.Remove(LevelInstanceActor);
	// Unload right away
	UnloadLevelInstance(LevelInstanceActor->GetLevelInstanceID());
		
	// Load Edit LevelInstance level
	ULevelStreamingLevelInstanceEditor* LevelStreaming = ULevelStreamingLevelInstanceEditor::Load(LevelInstanceActor);
	FLevelInstanceEdit& LevelInstanceEdit = LevelInstanceEdits.Add(FName(*LevelInstanceActor->GetWorldAssetPackage()));
	LevelInstanceEdit.LevelStreaming = LevelStreaming;
		
	// Try and select something meaningful
	AActor* ActorToSelect = nullptr;
	if (!ActorNameToSelect.IsEmpty())
	{		
		ActorToSelect = FindObject<AActor>(LevelStreaming->GetLoadedLevel(), *ActorNameToSelect);
	}

	// default to LevelInstance
	if (!ActorToSelect)
	{
		ActorToSelect = LevelInstanceActor;
	}
	LevelInstanceActor->SetIsTemporarilyHiddenInEditor(false);

	// Notify
	LevelInstanceActor->OnEdit();

	// Pending edit notification was already sent
	if (!bWasPendingEdit)
	{
		ForEachLevelInstanceAncestors(LevelInstanceActor, [](ALevelInstance* AncestorLevelInstance)
		{
			AncestorLevelInstance->OnEditChild();
			return true;
		});
	}

	GEditor->SelectActor(ActorToSelect, true, true);

	for (const auto& Actor : LevelStreaming->LoadedLevel->Actors)
	{
		const bool bEditing = true;
		Actor->PushLevelInstanceEditingStateToProxies(bEditing);
	}

	const bool bHasLevelInstanceEdits = LevelInstanceEdits.Num() > 0;
	if (bHasLevelInstanceEdits)
	{
		for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
		{
			if (LevelVC && LevelVC->GetWorld() == GetWorld())
			{
				LevelVC->EngineShowFlags.EditingLevelInstance = bHasLevelInstanceEdits;
			}
		}
	}
}

void ULevelInstanceSubsystem::CommitChildrenLevelInstances(ALevelInstance* LevelInstanceActor)
{
	// We are ending editing. Discard Non dirty child edits
	ForEachLevelInstanceChildren(LevelInstanceActor, /*bRecursive=*/true, [this](const ALevelInstance* ChildLevelInstanceActor)
	{
		if (const FLevelInstanceEdit* ChildLevelInstanceEdit = GetLevelInstanceEdit(ChildLevelInstanceActor))
		{
			check(!IsLevelInstanceEditDirty(ChildLevelInstanceEdit));
			ULevelStreamingLevelInstanceEditor::Unload(ChildLevelInstanceEdit->LevelStreaming);
			LevelInstanceEdits.Remove(FName(*ChildLevelInstanceActor->GetWorldAssetPackage()));
		}
		return true;
	});
}

ALevelInstance* ULevelInstanceSubsystem::CommitLevelInstance(ALevelInstance* LevelInstanceActor, bool bDiscardEdits)
{
	check(CanCommitLevelInstance(LevelInstanceActor));

	const FLevelInstanceEdit* LevelInstanceEdit = GetLevelInstanceEdit(LevelInstanceActor);
	check(LevelInstanceEdit);
	UWorld* EditingWorld = LevelInstanceEdit->GetEditWorld();
	check(EditingWorld);
			
	bool bChangesCommitted = false;
	if (IsLevelInstanceEditDirty(LevelInstanceEdit) && !bDiscardEdits)
	{
		const bool bPromptUserToSave = true;
		const bool bSaveMapPackages = true;
		const bool bSaveContentPackages = true;
		const bool bFastSave = false;
		const bool bNotifyNoPackagesSaved = false;
		const bool bCanBeDeclined = true;

		if (!FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined, nullptr,
			[=](UPackage* DirtyPackage)
			{
				return ShouldIgnoreDirtyPackage(DirtyPackage, EditingWorld);
			}))
		{
			return LevelInstanceActor;
		}

		// Validate that we indeed need to refresh instances (user can cancel the changes when prompted)
		bChangesCommitted = !IsLevelInstanceEditDirty(LevelInstanceEdit);

		if(bChangesCommitted)
		{
			// Sync the AssetData so that the updated instances have the latest Actor Registry Data
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.ScanPathsSynchronous({ LevelInstanceActor->GetWorldAssetPackage() }, true);
			// Notify 
			LevelInstanceActor->OnWorldAssetSaved();

			// Update pointer since BP Compilation might have invalidated LevelInstanceActor
			LevelInstanceActor = LevelInstanceEdit->LevelStreaming->GetLevelInstanceActor();
		}
	}

	FScopedSlowTask SlowTask(0, LOCTEXT("EndEditLevelInstance", "Unloading edit Level Instance..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	GEditor->SelectNone(false, true);
		
	// End edit non dirty child edits
	CommitChildrenLevelInstances(LevelInstanceActor);
		
	// Try to find proper LevelInstance to select
	TWeakObjectPtr<ALevelInstance> ActorToSelect = LevelInstanceActor;
	
	ForEachLevelInstanceAncestors(LevelInstanceActor, [&ActorToSelect](ALevelInstance* AncestorLevelInstance)
	{
		// if we find a parent editing LevelInstance this is what we want to select
		if (AncestorLevelInstance->IsEditing())
		{
			ActorToSelect = AncestorLevelInstance;
			return false;
		}
		else // if not we go up the ancestor LevelInstances to the highest level
		{
			ActorToSelect = AncestorLevelInstance;
		}
		return true;
	});

	const FString EditPackage = LevelInstanceActor->GetWorldAssetPackage();

	// Remove from streaming level...
	ULevelStreamingLevelInstanceEditor::Unload(LevelInstanceEdit->LevelStreaming);
	LevelInstanceEdits.Remove(FName(*EditPackage));

	// Notify (Actor might get destroyed by this call if its a packed bp
	LevelInstanceActor->OnCommit();

	// Notify Ancestors
	ForEachLevelInstanceAncestors(LevelInstanceActor, [](ALevelInstance* AncestorLevelInstance)
	{
		AncestorLevelInstance->OnCommitChild();
		return true;
	});
				
	// Propagate to other instances
	for (TObjectIterator<UWorld> It(RF_ClassDefaultObject|RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill); It; ++It)
	{
		UWorld* CurrentWorld = *It;
		if (CurrentWorld->GetSubsystem<ULevelInstanceSubsystem>() != nullptr)
		{
			for (TActorIterator<ALevelInstance> LevelInstanceIt(CurrentWorld); LevelInstanceIt; ++LevelInstanceIt)
			{
				ALevelInstance* CurrentLevelInstanceActor = *LevelInstanceIt;
				if (CurrentLevelInstanceActor->GetWorldAssetPackage() == EditPackage && (LevelInstanceActor == CurrentLevelInstanceActor || bChangesCommitted))
				{
					CurrentLevelInstanceActor->UpdateLevelInstance();
				}
			}
		}
	}

	if (ALevelInstance* Actor = ActorToSelect.Get())
	{
		GEditor->SelectActor(Actor, true, true);
	}

	const bool bHasLevelInstanceEdits = LevelInstanceEdits.Num() > 0;

	if (!bHasLevelInstanceEdits)
	{
		for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
		{
			if (LevelVC && LevelVC->GetWorld() == GetWorld())
			{
				LevelVC->EngineShowFlags.EditingLevelInstance = bHasLevelInstanceEdits;
			}
		}
	}

	return LevelInstanceActor;
}

void ULevelInstanceSubsystem::SaveLevelInstanceAs(ALevelInstance* LevelInstanceActor)
{
	check(CanCommitLevelInstance(LevelInstanceActor));

	const FLevelInstanceEdit* OldLevelInstanceEdit = GetLevelInstanceEdit(LevelInstanceActor);
	check(OldLevelInstanceEdit);
	UWorld* EditingWorld = OldLevelInstanceEdit->GetEditWorld();
	check(EditingWorld);

	// Reset the level transform before saving
	OldLevelInstanceEdit->LevelStreaming->GetLoadedLevel()->ApplyWorldOffset(-LevelInstanceActor->GetTransform().GetLocation(), false);

	TArray<UObject*> OutObjects;
	FEditorFileUtils::SaveAssetsAs({ EditingWorld }, OutObjects);

	if (OutObjects.Num() == 0 || OutObjects[0] == EditingWorld)
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("Failed to save Level Instance as new asset"));
		return;
	}

	UWorld* SavedWorld = StaticCast<UWorld*>(OutObjects[0]);
	// Discard edits and unload streaming level
	DiscardEdits();
	
	LevelInstanceActor->SetWorldAsset(SavedWorld);

	LoadLevelInstance(LevelInstanceActor);
	GEditor->SelectActor(LevelInstanceActor, true, true);
}

ALevelInstance* ULevelInstanceSubsystem::GetParentLevelInstance(const AActor* Actor) const
{
	check(Actor);
	const ULevel* OwningLevel = Actor->GetLevel();
	check(OwningLevel);
	return GetOwningLevelInstance(OwningLevel);
}

void ULevelInstanceSubsystem::BlockLoadLevelInstance(ALevelInstance* LevelInstanceActor)
{
	check(!LevelInstanceActor->IsEditing());
	RequestLoadLevelInstance(LevelInstanceActor, true);

	// Make sure blocking loads can happen and are not part of transaction
	TGuardValue<ITransaction*> TransactionGuard(GUndo, nullptr);

	// Blocking until LevelInstance is loaded and all its child LevelInstances
	while (LevelInstancesToLoadOrUpdate.Num())
	{
		UpdateStreamingState();
	}
}
#endif

#undef LOCTEXT_NAMESPACE

