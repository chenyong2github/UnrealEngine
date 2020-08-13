// Copyright Epic Games, Inc. All Rights Reserved.

#include "Foundation/FoundationSubsystem.h"
#include "Foundation/FoundationActor.h"
#include "Foundation/FoundationInstanceLevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "FoundationPrivate.h"
#include "LevelUtils.h"
#include "Misc/HashBuilder.h"

#if WITH_EDITOR
#include "Foundation/FoundationEditorLevelStreaming.h"
#include "WorldPartition/Foundation/FoundationActorDescFactory.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetRegistryModule.h"
#include "ActorRegistry.h"
#include "AssetData.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Foundation/IFoundationEditorModule.h"
#include "HAL/PlatformTime.h"
#endif

#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FoundationSubsystem"

DEFINE_LOG_CATEGORY(LogFoundation);

UFoundationSubsystem::UFoundationSubsystem()
	: UWorldSubsystem()
{

}

UFoundationSubsystem::~UFoundationSubsystem()
{

}

void UFoundationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	FoundationActorDescFactory.Reset(new FFoundationActorDescFactory());

	Collection.InitializeDependency(UWorldPartitionSubsystem::StaticClass());
	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>())
	{
		RegisterActorDescFactories(WorldPartitionSubsystem);
	}

	if (GEditor)
	{
		FModuleManager::LoadModuleChecked<IFoundationEditorModule>("FoundationEditor");
	}
#endif
}

void UFoundationSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

AFoundationActor* UFoundationSubsystem::GetFoundation(FFoundationID FoundationID) const
{
	if (AFoundationActor*const* FoundationActor = RegisteredFoundations.Find(FoundationID))
	{
		return *FoundationActor;
	}

	return nullptr;
}

FFoundationID UFoundationSubsystem::ComputeFoundationID(AFoundationActor* FoundationActor) const
{
	FFoundationID FoundationID = InvalidFoundationID;
	FHashBuilder HashBuilder;
	ForEachFoundationAncestorsAndSelf(FoundationActor, [&HashBuilder](const AFoundationActor* AncestorOrSelf)
	{
		HashBuilder << AncestorOrSelf->GetFoundationActorGuid();
		return true;
	});
	FoundationID = HashBuilder.GetHash();
	return FoundationID;
}

FFoundationID UFoundationSubsystem::RegisterFoundation(AFoundationActor* FoundationActor)
{
	FFoundationID FoundationID = ComputeFoundationID(FoundationActor);
	check(FoundationID != InvalidFoundationID);
	AFoundationActor*& Value = RegisteredFoundations.FindOrAdd(FoundationID);
	check(GIsReinstancing || Value == nullptr || Value == FoundationActor);
	Value = FoundationActor;
	return FoundationID;
}

void UFoundationSubsystem::UnregisterFoundation(AFoundationActor* FoundationActor)
{
	RegisteredFoundations.Remove(FoundationActor->GetFoundationID());
}

void UFoundationSubsystem::RequestLoadFoundation(AFoundationActor* FoundationActor, bool bForce /* = false */)
{
	check(FoundationActor && !FoundationActor->IsPendingKillOrUnreachable());
	if (FoundationActor->IsFoundationPathValid())
	{
#if WITH_EDITOR
		if (!IsEditingFoundation(FoundationActor))
#endif
		{
			FoundationsToUnload.Remove(FoundationActor->GetFoundationID());

			bool* bForcePtr = FoundationsToLoadOrUpdate.Find(FoundationActor);

			// Avoid loading if already loaded. Can happen if actor requests unload/load in same frame. Without the force it means its not necessary.
			if (IsLoaded(FoundationActor) && !bForce && (bForcePtr == nullptr || !(*bForcePtr)))
			{
				return;
			}

			if (bForcePtr != nullptr)
			{
				*bForcePtr |= bForce;
			}
			else
			{
				FoundationsToLoadOrUpdate.Add(FoundationActor, bForce);
			}
		}
	}
}

void UFoundationSubsystem::RequestUnloadFoundation(AFoundationActor* FoundationActor)
{
	const FFoundationID& FoundationID = FoundationActor->GetFoundationID();
	if (FoundationInstances.Contains(FoundationID))
	{
		// FoundationsToUnload uses FFoundationID because FoundationActor* can be destroyed in later Tick and we don't need it.
		FoundationsToUnload.Add(FoundationID);
	}
	FoundationsToLoadOrUpdate.Remove(FoundationActor);
}

bool UFoundationSubsystem::IsLoaded(const AFoundationActor* FoundationActor) const
{
	return FoundationActor->HasValidFoundationID() && FoundationInstances.Contains(FoundationActor->GetFoundationID());
}

void UFoundationSubsystem::Tick(float DeltaSeconds)
{
#if WITH_EDITOR
	// For non-game world, Tick is responsible of processing foundations to update/load/unload
	if (!GetWorld()->IsGameWorld())
	{
		UpdateStreamingState();

		// Begin editing the pending foundation when loads complete
		if (PendingFoundationToEdit != InvalidFoundationID && !FoundationsToLoadOrUpdate.Num())
		{
			if (AFoundationActor** FoundationActor = RegisteredFoundations.Find(PendingFoundationToEdit))
			{
				EditFoundation(*FoundationActor);
			}
		}
	}
#endif
}

bool UFoundationSubsystem::IsTickableInEditor() const
{
	return true;
}

UWorld* UFoundationSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

ETickableTickType UFoundationSubsystem::GetTickableTickType() const
{
#if WITH_EDITOR
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
#else
	return ETickableTickType::Never;
#endif
}

TStatId UFoundationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFoundationSubsystem, STATGROUP_Tickables);
}

void UFoundationSubsystem::UpdateStreamingState()
{
	if (!FoundationsToUnload.Num() && !FoundationsToLoadOrUpdate.Num())
	{
		return;
	}

#if WITH_EDITOR
	// Do not update during transaction
	if (GUndo)
	{
		return;
	}

	FScopedSlowTask SlowTask(FoundationsToUnload.Num() + FoundationsToLoadOrUpdate.Num() * 2, LOCTEXT("UpdatingFoundations", "Updating Foundations..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	check(!LevelsToRemoveScope);
	LevelsToRemoveScope.Reset(new FLevelsToRemoveScope());
#endif

	if (FoundationsToUnload.Num())
	{
		TSet<FFoundationID> FoundationsToUnloadCopy(MoveTemp(FoundationsToUnload));
		for (const FFoundationID& FoundationID : FoundationsToUnloadCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("UnloadingFoundation", "Unloading Foundation"));
#endif
			UnloadFoundation(FoundationID);
		}
	}

	if (FoundationsToLoadOrUpdate.Num())
	{
		// Unload levels before doing any loading
		TMap<AFoundationActor*, bool> FoundationsToLoadOrUpdateCopy(MoveTemp(FoundationsToLoadOrUpdate));
		for (const TPair<AFoundationActor*, bool>& Pair : FoundationsToLoadOrUpdateCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("UnloadingFoundation", "Unloading Foundation"));
#endif
			AFoundationActor* FoundationActor = Pair.Key;
			if (Pair.Value)
			{
				UnloadFoundation(FoundationActor->GetFoundationID());
			}
		}

#if WITH_EDITOR
		LevelsToRemoveScope.Reset();
		double StartTime = FPlatformTime::Seconds();
#endif
		for (const TPair<AFoundationActor*, bool>& Pair : FoundationsToLoadOrUpdateCopy)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1.f, LOCTEXT("LoadingFoundation", "Loading Foundation"));
#endif
			LoadFoundation(Pair.Key);
		}
#if WITH_EDITOR
		double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogFoundation, Log, TEXT("Loaded %s levels in %s seconds"), *FText::AsNumber(FoundationsToLoadOrUpdateCopy.Num()).ToString(), *FText::AsNumber(ElapsedTime).ToString());
#endif
	}

#if WITH_EDITOR
	LevelsToRemoveScope.Reset();
#endif
}

void UFoundationSubsystem::LoadFoundation(AFoundationActor* FoundationActor)
{
	check(FoundationActor);
	if (IsLoaded(FoundationActor) || FoundationActor->IsPendingKillOrUnreachable() || !FoundationActor->IsFoundationPathValid())
	{
		return;
	}

	const FFoundationID& FoundationID = FoundationActor->GetFoundationID();
	check(!FoundationInstances.Contains(FoundationID));

	if (ULevelStreamingFoundationInstance* LevelStreaming = ULevelStreamingFoundationInstance::LoadInstance(FoundationActor))
	{
		FFoundationInstance& FoundationInstance = FoundationInstances.Add(FoundationID);
		FoundationInstance.LevelStreaming = LevelStreaming;
#if WITH_EDITOR
		FoundationActor->OnFoundationLoaded();
#endif
	}
}

void UFoundationSubsystem::UnloadFoundation(const FFoundationID& FoundationID)
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

	FFoundationInstance FoundationInstance;
	if (FoundationInstances.RemoveAndCopyValue(FoundationID, FoundationInstance))
	{
		if (ULevel* LoadedLevel = FoundationInstance.LevelStreaming->GetLoadedLevel())
		{
			ForEachActorInLevel(LoadedLevel, [this](AActor* LevelActor)
			{
				if (AFoundationActor* FoundationActor = Cast<AFoundationActor>(LevelActor))
				{
					UnloadFoundation(FoundationActor->GetFoundationID());
				}
				return true;
			});
		}

		ULevelStreamingFoundationInstance::UnloadInstance(FoundationInstance.LevelStreaming);
	}

#if WITH_EDITOR
	if (bReleaseScope)
	{
		LevelsToRemoveScope.Reset();
	}
#endif
}

void UFoundationSubsystem::ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const
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

void UFoundationSubsystem::ForEachFoundationAncestorsAndSelf(AActor* Actor, TFunctionRef<bool(AFoundationActor*)> Operation) const
{
	if (AFoundationActor* FoundationActor = Cast<AFoundationActor>(Actor))
	{
		if (!Operation(FoundationActor))
		{
			return;
		}
	}

	ForEachFoundationAncestors(Actor, Operation);
}

void UFoundationSubsystem::ForEachFoundationAncestors(AActor* Actor, TFunctionRef<bool(AFoundationActor*)> Operation) const
{
	AFoundationActor* ParentFoundation = nullptr;
	do
	{
		ParentFoundation = GetOwningFoundation(Actor->GetLevel());
		Actor = ParentFoundation;

	} while (ParentFoundation != nullptr && Operation(ParentFoundation));
}

AFoundationActor* UFoundationSubsystem::GetOwningFoundation(const ULevel* Level) const
{
	if (ULevelStreaming* BaseLevelStreaming = FLevelUtils::FindStreamingLevel(Level))
	{
#if WITH_EDITOR
		if (ULevelStreamingFoundationEditor* LevelStreamingEditor = Cast<ULevelStreamingFoundationEditor>(BaseLevelStreaming))
		{
			return LevelStreamingEditor->GetFoundationActor();
		}
		else 
#endif
		if (ULevelStreamingFoundationInstance* LevelStreaming = Cast<ULevelStreamingFoundationInstance>(BaseLevelStreaming))
		{
			return LevelStreaming->GetFoundationActor();
		}
		else if (UWorldPartitionLevelStreamingDynamic* WorldPartitionLevelStreaming = Cast<UWorldPartitionLevelStreamingDynamic>(BaseLevelStreaming))
		{
			return GetOwningFoundation(WorldPartitionLevelStreaming->GetOuterWorld()->PersistentLevel);
		}
	}

	return nullptr;
}

#if WITH_EDITOR

bool UFoundationSubsystem::GetFoundationBounds(const AFoundationActor* FoundationActor, FBox& OutBounds) const
{
	if (IsLoaded(FoundationActor))
	{
		const FFoundationInstance& FoundationInstance = FoundationInstances.FindChecked(FoundationActor->GetFoundationID());
		OutBounds = FoundationInstance.LevelStreaming->GetBounds();
		return true;
	}
	else if(FoundationActor->IsFoundationPathValid())
	{
		if (ULevel::GetLevelBoundsFromPackage(FName(*FoundationActor->GetFoundationPackage()), OutBounds))
		{
			return true;
		}
	}

	return false;
}

void UFoundationSubsystem::ForEachActorInFoundation(const AFoundationActor* FoundationActor, TFunctionRef<bool(AActor * LevelActor)> Operation) const
{
	if (ULevel* FoundationLevel = GetFoundationLevel(FoundationActor))
	{
		ForEachActorInLevel(FoundationLevel, Operation);
	}
}

void UFoundationSubsystem::ForEachFoundationAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const AFoundationActor*)> Operation) const
{
	if (const AFoundationActor* FoundationActor = Cast<AFoundationActor>(Actor))
	{
		if (!Operation(FoundationActor))
		{
			return;
		}
	}

	ForEachFoundationAncestors(Actor, Operation);
}

void UFoundationSubsystem::ForEachFoundationAncestors(const AActor* Actor, TFunctionRef<bool(const AFoundationActor*)> Operation) const
{
	const AFoundationActor* ParentFoundation = nullptr;
	do 
	{
		ParentFoundation = GetOwningFoundation(Actor->GetLevel());
		Actor = ParentFoundation;
	} 
	while (ParentFoundation != nullptr && Operation(ParentFoundation));
}

void UFoundationSubsystem::ForEachFoundationChildren(const AFoundationActor* FoundationActor, bool bRecursive, TFunctionRef<bool(const AFoundationActor*)> Operation) const
{
	if (ULevel* FoundationLevel = GetFoundationLevel(FoundationActor))
	{
		ForEachActorInLevel(FoundationLevel, [this, Operation,bRecursive](AActor* LevelActor)
		{
			if (const AFoundationActor* ChildFoundationActor = Cast<AFoundationActor>(LevelActor))
			{
				if (Operation(ChildFoundationActor) && bRecursive)
				{
					ForEachFoundationChildren(ChildFoundationActor, bRecursive, Operation);
				}
			}
			return true;
		});
	}
}

void UFoundationSubsystem::ForEachFoundationChildren(AFoundationActor* FoundationActor, bool bRecursive, TFunctionRef<bool(AFoundationActor*)> Operation) const
{
	if (ULevel* FoundationLevel = GetFoundationLevel(FoundationActor))
	{
		ForEachActorInLevel(FoundationLevel, [this, Operation, bRecursive](AActor* LevelActor)
		{
			if (AFoundationActor* ChildFoundationActor = Cast<AFoundationActor>(LevelActor))
			{
				if (Operation(ChildFoundationActor) && bRecursive)
				{
					ForEachFoundationChildren(ChildFoundationActor, bRecursive, Operation);
				}
			}
			return true;
		});
	}
}

void UFoundationSubsystem::ForEachFoundationEdit(TFunctionRef<bool(AFoundationActor*)> Operation) const
{
	for (const auto& Pair : FoundationEdits)
	{
		if (!Operation(Pair.Value.LevelStreaming->GetFoundationActor()))
		{
			return;
		}
	}
}

bool UFoundationSubsystem::HasDirtyChildrenFoundations(const AFoundationActor* FoundationActor) const
{
	bool bDirtyChildren = false;
	ForEachFoundationChildren(FoundationActor, /*bRecursive=*/true, [this, &bDirtyChildren](const AFoundationActor* ChildFoundationActor)
	{
		if (IsEditingFoundationDirty(ChildFoundationActor))
		{
			bDirtyChildren = true;
			return false;
		}
		return true;
	});
	return bDirtyChildren;
}

void UFoundationSubsystem::SetIsTemporarilyHiddenInEditor(AFoundationActor* FoundationActor, bool bIsHidden)
{
	if (ULevel* FoundationLevel = GetFoundationLevel(FoundationActor))
	{
		ForEachActorInLevel(FoundationLevel, [bIsHidden](AActor* LevelActor)
		{
			LevelActor->SetIsTemporarilyHiddenInEditor(bIsHidden);
			return true;
		});
	}
}

bool UFoundationSubsystem::SetCurrent(AFoundationActor* FoundationActor) const
{
	if (IsEditingFoundation(FoundationActor))
	{
		return GetWorld()->SetCurrentLevel(GetFoundationLevel(FoundationActor));
	}

	return false;
}

bool UFoundationSubsystem::IsCurrent(const AFoundationActor* FoundationActor) const
{
	if (IsEditingFoundation(FoundationActor))
	{
		return GetFoundationLevel(FoundationActor) == GetWorld()->GetCurrentLevel();
	}

	return false;
}

bool UFoundationSubsystem::MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel) const
{
	check(DestinationLevel);

	const bool bWarnAboutReferences = true;
	const bool bWarnAboutRenaming = true;
	const bool bMoveAllOrFail = true;
	if (!EditorLevelUtils::MoveActorsToLevel(ActorsToRemove, DestinationLevel, bWarnAboutReferences, bWarnAboutRenaming, bMoveAllOrFail))
	{
		UE_LOG(LogFoundation, Warning, TEXT("Failed to move actors out of foundation because not all actors could be moved"));
		return false;
	}
	return true;
}

bool UFoundationSubsystem::MoveActorsTo(AFoundationActor* FoundationActor, const TArray<AActor*>& ActorsToMove)
{
	check(IsEditingFoundation(FoundationActor));
	ULevel* FoundationLevel = GetFoundationLevel(FoundationActor);
	check(FoundationLevel);

	return MoveActorsToLevel(ActorsToMove, FoundationLevel);
}

AFoundationActor* UFoundationSubsystem::CreateFoundationFrom(const TArray<AActor*>& ActorsToMove, UWorld* TemplateWorld)
{
	ULevel* CurrentLevel = GetWorld()->GetCurrentLevel();
		
	if (ActorsToMove.Num() == 0)
	{
		UE_LOG(LogFoundation, Warning, TEXT("Failed to create foundation from empty actor array"));
		return nullptr;
	}
		
	FBox ActorLocationBox(ForceInit);
	for (const AActor* ActorToMove : ActorsToMove)
	{
		const bool bNonColliding = false;
		const bool bIncludeChildren = true;
		ActorLocationBox += ActorToMove->GetComponentsBoundingBox(bNonColliding, bIncludeChildren);

		if (!CanMoveActorToLevel(ActorToMove))
		{
			return nullptr;
		}
	}

	FVector FoundationLocation = ActorLocationBox.GetCenter();
	FoundationLocation.Z = ActorLocationBox.Min.Z;

	ULevelStreamingFoundationEditor* LevelStreaming = StaticCast<ULevelStreamingFoundationEditor*>(EditorLevelUtils::CreateNewStreamingLevelForWorld(*GetWorld(), ULevelStreamingFoundationEditor::StaticClass(), TEXT(""), false, TemplateWorld));
	if (!LevelStreaming)
	{
		UE_LOG(LogFoundation, Warning, TEXT("Failed to create new foundation level"));
		return nullptr;
	}

	ULevel* LoadedLevel = LevelStreaming->GetLoadedLevel();
	check(LoadedLevel);

	const bool bWarnAboutReferences = true;
	const bool bWarnAboutRenaming = true;
	const bool bMoveAllOrFail = true;

	if (!EditorLevelUtils::MoveActorsToLevel(ActorsToMove, LoadedLevel, bWarnAboutReferences, bWarnAboutRenaming, bMoveAllOrFail))
	{
		ULevelStreamingFoundationEditor::Unload(LevelStreaming);
		UE_LOG(LogFoundation, Warning, TEXT("Failed to create foundation because some actors couldn't be moved"));
		return nullptr;
	}
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.bCreateActorPackage = true;
	SpawnParams.OverrideLevel = CurrentLevel;
	AFoundationActor* NewFoundationActor = GetWorld()->SpawnActor<AFoundationActor>(AFoundationActor::StaticClass(), SpawnParams);
	check(NewFoundationActor);
	NewFoundationActor->SetFoundation(LoadedLevel->GetTypedOuter<UWorld>());
	NewFoundationActor->SetActorLocation(FoundationLocation);

	// Actors were moved and kept their World positions so when saving we want their positions to actually be relative to the FounationActor/LevelTransform
	// so we set the LevelTransform and we mark the level as having moved its actors. 
	// On Level save FLevelUtils::RemoveEditorTransform will fixup actor transforms to make them relative to the LevelTransform.
	LevelStreaming->LevelTransform = NewFoundationActor->GetActorTransform();
	LoadedLevel->bAlreadyMovedActors = true;

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(NewFoundationActor, true, true);

	FFoundationEdit& FoundationEdit = FoundationEdits.Add(FName(*NewFoundationActor->GetFoundationPackage()));
	FoundationEdit.LevelStreaming = LevelStreaming;
	LevelStreaming->FoundationID = NewFoundationActor->GetFoundationID();

	GetWorld()->SetCurrentLevel(LoadedLevel);

	if (!UEditorLoadingAndSavingUtils::SavePackages({ LoadedLevel->GetOutermost() }, true))
	{
		UE_LOG(LogFoundation, Error, TEXT("Failed to save foundation (%s)"), *NewFoundationActor->GetFoundationPackage());
	}
	else
	{
		UE_LOG(LogFoundation, Log, TEXT("Foundation create successfully (%s)"), *NewFoundationActor->GetFoundationPackage());
	}

	// Exit edit
	CommitFoundation(NewFoundationActor);

	return NewFoundationActor;
}

ULevel* UFoundationSubsystem::GetFoundationLevel(const AFoundationActor* FoundationActor) const
{
	if (FoundationActor->HasValidFoundationID())
	{
		if (const FFoundationEdit* FoundationEdit = GetFoundationEdit(FoundationActor))
		{
			return FoundationEdit->LevelStreaming->GetLoadedLevel();
		}
		else if (const FFoundationInstance* FoundationInstance = FoundationInstances.Find(FoundationActor->GetFoundationID()))
		{
			return FoundationInstance->LevelStreaming->GetLoadedLevel();
		}
	}

	return nullptr;
}

void UFoundationSubsystem::RemoveLevelFromWorld(ULevel* Level, bool bResetTrans)
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

UFoundationSubsystem::FLevelsToRemoveScope::~FLevelsToRemoveScope()
{
	if (Levels.Num() > 0)
	{
		double StartTime = FPlatformTime::Seconds();
		const bool bClearSelection = false;
		// No need to clear the whole editor selection since actor of this level will be removed from the selection by: UEditorEngine::OnLevelRemovedFromWorld
		EditorLevelUtils::RemoveLevelsFromWorld(Levels, bClearSelection, true);
		double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogFoundation, Log, TEXT("Unloaded %s levels in %s seconds"), *FText::AsNumber(Levels.Num()).ToString(), *FText::AsNumber(ElapsedTime).ToString());
	}
}

bool UFoundationSubsystem::CanMoveActorToLevel(const AActor* Actor) const
{
	if (Actor->GetWorld() == GetWorld())
	{
		if (const AFoundationActor* FoundationActor = Cast<AFoundationActor>(Actor))
		{
			if (IsEditingFoundation(FoundationActor))
			{
				UE_LOG(LogFoundation, Warning, TEXT("Can't move foundation actor while it is being edited"));
				return false;
			}

			bool bEditingChildren = false;
			ForEachFoundationChildren(FoundationActor, true, [this, &bEditingChildren](const AFoundationActor* ChildFoundationActor)
			{
				if (IsEditingFoundation(ChildFoundationActor))
				{
					bEditingChildren = true;
					return false;
				}
				return true;
			});

			if (bEditingChildren)
			{
				UE_LOG(LogFoundation, Warning, TEXT("Can't move foundation actor while one of its child foundation is being edited"));
				return false;
			}
		}
	}

	return true;
}

void UFoundationSubsystem::DiscardEdits()
{
	for (const auto& Pair : FoundationEdits)
	{
		ULevelStreamingFoundationEditor::Unload(Pair.Value.LevelStreaming);
	}
	FoundationEdits.Empty();
}

void UFoundationSubsystem::OnActorDeleted(AActor* Actor)
{
	if (AFoundationActor* FoundationActor = Cast<AFoundationActor>(Actor))
	{
		if (Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			// We are receiving this event when destroying the old actor after BP reinstantiation. In this case,
			// the newly created actor was already added to the list, so we can safely ignore this case.
			check(GIsReinstancing);
			return;
		}

		const bool bAlreadyRooted = FoundationActor->IsRooted();
		// Unloading Foundations leads to GC and Actor can be collected. Add to root temp. It will get collected after the OnActorDeleted callbacks
		if (!bAlreadyRooted)
		{
			FoundationActor->AddToRoot();
		}

		FScopedSlowTask SlowTask(0, LOCTEXT("UnloadingFoundations", "Unloading Foundations..."), !GetWorld()->IsGameWorld());
		SlowTask.MakeDialog();
		check(!IsEditingFoundationDirty(FoundationActor) && !HasDirtyChildrenFoundations(FoundationActor));
		if (IsEditingFoundation(FoundationActor))
		{
			CommitFoundation(FoundationActor);
		}
		CommitChildrenFoundations(FoundationActor);
		FoundationsToLoadOrUpdate.Remove(FoundationActor);
				
		UnloadFoundation(FoundationActor->GetFoundationID());
		
		// Remove from root so it gets collected on the next GC if it can be.
		if (!bAlreadyRooted)
		{
			FoundationActor->RemoveFromRoot();
		}
	}
}

void UFoundationSubsystem::RegisterActorDescFactories(UWorldPartitionSubsystem* WorldPartitionSubsystem)
{
	WorldPartitionSubsystem->RegisterActorDescFactory(AFoundationActor::StaticClass(), FoundationActorDescFactory.Get());
}

bool UFoundationSubsystem::ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld)
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

UWorld* UFoundationSubsystem::FFoundationEdit::GetEditWorld() const
{
	if (LevelStreaming && LevelStreaming->GetLoadedLevel())
	{
		return LevelStreaming->GetLoadedLevel()->GetTypedOuter<UWorld>();
	}

	return nullptr;
}

const UFoundationSubsystem::FFoundationEdit* UFoundationSubsystem::GetFoundationEdit(const AFoundationActor* FoundationActor) const
{
	const FFoundationEdit* FoundationEdit = FoundationEdits.Find(FName(*FoundationActor->GetFoundationPackage()));
	if (!FoundationEdit || FoundationEdit->LevelStreaming->GetFoundationActor() != FoundationActor)
	{
		return nullptr;
	}

	return FoundationEdit;
}

bool UFoundationSubsystem::IsEditingFoundationDirty(const AFoundationActor* FoundationActor) const
{
	const FFoundationEdit* FoundationEdit = GetFoundationEdit(FoundationActor);
	if (!FoundationEdit)
	{
		return false;
	}

	return IsFoundationEditDirty(FoundationEdit);
}

bool UFoundationSubsystem::IsFoundationEditDirty(const FFoundationEdit* FoundationEdit) const
{
	const UWorld* EditingWorld = FoundationEdit->GetEditWorld();
	check(EditingWorld);

	TArray<UPackage*> OutDirtyPackages;
	FEditorFileUtils::GetDirtyPackages(OutDirtyPackages, [EditingWorld](UPackage* DirtyPackage)
	{
		return UFoundationSubsystem::ShouldIgnoreDirtyPackage(DirtyPackage, EditingWorld);
	});

	return OutDirtyPackages.Num() > 0;
}

bool UFoundationSubsystem::CanEditFoundation(const AFoundationActor* FoundationActor, FText* OutReason) const
{
	if (ULevel* FoundationLevel = GetFoundationLevel(FoundationActor))
	{
		if (FoundationLevel->GetWorldPartition())
		{
			if (OutReason)
			{
				*OutReason = LOCTEXT("CanEditPartitionedFoundation", "Can't edit partitioned Foundation");
			}
			return false;
		}
	}

	if (const FFoundationEdit* FoundationEdit = FoundationEdits.Find(FName(*FoundationActor->GetFoundationPackage())))
	{
		if (OutReason)
		{
			if (FoundationEdit->LevelStreaming->GetFoundationActor() == FoundationActor)
			{
				*OutReason = LOCTEXT("CanEditFoundationAlreadyBeingEdited", "Foundation already being edited");
			}
			else
			{

				*OutReason = LOCTEXT("CanEditFoundationAlreadyEditing", "Another foundation pointing to the same level is being edited");
			}
		}
		return false;
	}

	// Do not allow multiple foundations of the same hierarchy to be edited... (checking ancestors)
	bool bAncestorBeingEdited = false;
	if (FoundationEdits.Num() > 0)
	{
		ForEachFoundationAncestors(FoundationActor, [this, &bAncestorBeingEdited, OutReason](const AFoundationActor* AncestorFoundation)
		{
			if (const FFoundationEdit* FoundationEdit = FoundationEdits.Find(FName(*AncestorFoundation->GetFoundationPackage())))
			{
				// Allow children to be edited if ancestor is clean
				if (IsFoundationEditDirty(FoundationEdit))
				{
					if (OutReason)
					{
						*OutReason = LOCTEXT("CanEditFoundationAncestorBeingEdited", "Ancestor Foundation already being edited");
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
		
	// Do not allow multiple foundations of the same hierarchy to be edited... (checking children)
	bool bChildBeingEdited = false;
	if (FoundationEdits.Num() > 0)
	{
		for (const auto& Pair : FoundationEdits)
		{
			check(Pair.Value.LevelStreaming);
			AFoundationActor* FoundationEditActor = Pair.Value.LevelStreaming->GetFoundationActor();
			check(FoundationEditActor);
			ForEachFoundationAncestors(FoundationEditActor, [this, &bChildBeingEdited, OutReason, FoundationActor](const AFoundationActor* AncestorFoundation)
			{
				if (AncestorFoundation->GetFoundation() == FoundationActor->GetFoundation())
				{
					if (const FFoundationEdit* AncestorFoundationEdit = FoundationEdits.Find(FName(*AncestorFoundation->GetFoundationPackage())))
					{
						if (IsFoundationEditDirty(AncestorFoundationEdit))
						{
							if (OutReason)
							{
								*OutReason = LOCTEXT("CanEditFoundationOtherChildren", "Children Foundation already being edited");
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

	if (!FoundationActor->IsFoundationPathValid())
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanEditFoundationDirtyInvalid", "Foundation path is invalid");
		}
		return false;
	}

	if (FLevelUtils::FindStreamingLevel(GetWorld(), *FoundationActor->GetFoundationPackage()))
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanEditFoundationAlreadyExists", "The same level was added to world outside of Foundations");
		}
		return false;
	}

	return true;
}

bool UFoundationSubsystem::CanCommitFoundation(const AFoundationActor* FoundationActor, FText* OutReason) const
{
	if (!IsEditingFoundation(FoundationActor))
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("CanCommitFoundationNotEditing", "Foundation is not currently being edited");
		}
		return false;
	}

	return true;
}

void UFoundationSubsystem::EditFoundation(AFoundationActor* FoundationActor, TWeakObjectPtr<AActor> ContextActorPtr)
{
	check(CanEditFoundation(FoundationActor));
	PendingFoundationToEdit = InvalidFoundationID;
		
	FScopedSlowTask SlowTask(0, LOCTEXT("BeginEditFoundation", "Loading foundation for edit..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	// Gather information from the context actor to try and select something meaningful after the loading
	FString ActorNameToSelect;
	if (AActor* ContextActor = ContextActorPtr.Get())
	{
		ActorNameToSelect = ContextActor->GetName();
		ForEachFoundationAncestorsAndSelf(ContextActor, [&ActorNameToSelect,FoundationActor](const AFoundationActor* AncestorFoundationActor)
		{
			// stop when we hit the foundation we are about to edit
			if (AncestorFoundationActor == FoundationActor)
			{
				return false;
			}
			
			ActorNameToSelect = AncestorFoundationActor->GetName();
			return true;
		});
	}

	GEditor->SelectNone(false, true);

	// Check if there is an open (but clean) ancestor and child and unload it before opening the foundation for editing
	if (FoundationEdits.Num() > 0)
	{
		AFoundationActor* FoundationToCommit = nullptr;

		auto GetFoundationToCommit = [this, &FoundationToCommit](AFoundationActor* Foundation)
		{
			if (const FFoundationEdit* FoundationEdit = FoundationEdits.Find(FName(*Foundation->GetFoundationPackage())))
			{
				check(!IsFoundationEditDirty(FoundationEdit));
				check(FoundationToCommit == nullptr);
				FoundationToCommit = FoundationEdit->LevelStreaming->GetFoundationActor();
				check(FoundationToCommit != nullptr);
				return false;
			}
			return true;
		};

		ForEachFoundationAncestors(FoundationActor, GetFoundationToCommit);
		if (!FoundationToCommit)
		{
			ForEachFoundationChildren(FoundationActor, true, GetFoundationToCommit);
		}

		if (FoundationToCommit)
		{
			PendingFoundationToEdit = FoundationActor->GetFoundationID();
			CommitFoundation(FoundationToCommit);

			// Stop here. The foundation will be open for editing after an async reload.
			return;
		}
	}

	// Cleanup async requests in case
	FoundationsToUnload.Remove(FoundationActor->GetFoundationID());
	FoundationsToLoadOrUpdate.Remove(FoundationActor);
	// Unload right away
	UnloadFoundation(FoundationActor->GetFoundationID());
		
	// Load Edit foundation level
	ULevelStreamingFoundationEditor* LevelStreaming = ULevelStreamingFoundationEditor::Load(FoundationActor);
	FFoundationEdit& FoundationEdit = FoundationEdits.Add(FName(*FoundationActor->GetFoundationPackage()));
	FoundationEdit.LevelStreaming = LevelStreaming;

	// Try and select something meaningful
	AActor* ActorToSelect = nullptr;
	if (!ActorNameToSelect.IsEmpty())
	{		
		ActorToSelect = FindObject<AActor>(LevelStreaming->GetLoadedLevel(), *ActorNameToSelect);
	}

	// default to foundation
	if (!ActorToSelect)
	{
		ActorToSelect = FoundationActor;
	}
	FoundationActor->SetIsTemporarilyHiddenInEditor(false);
	GEditor->SelectActor(ActorToSelect, true, true);
}

void UFoundationSubsystem::CommitChildrenFoundations(AFoundationActor* FoundationActor)
{
	// We are ending editing. Discard Non dirty child edits
	ForEachFoundationChildren(FoundationActor, /*bRecursive=*/true, [this](const AFoundationActor* ChildFoundationActor)
	{
		if (const FFoundationEdit* ChildFoundationEdit = GetFoundationEdit(ChildFoundationActor))
		{
			check(!IsFoundationEditDirty(ChildFoundationEdit));
			ULevelStreamingFoundationEditor::Unload(ChildFoundationEdit->LevelStreaming);
			FoundationEdits.Remove(FName(*ChildFoundationActor->GetFoundationPackage()));
		}
		return true;
	});
}

void UFoundationSubsystem::CommitFoundation(AFoundationActor* FoundationActor, bool bDiscardEdits)
{
	check(CanCommitFoundation(FoundationActor));

	const FFoundationEdit* FoundationEdit = GetFoundationEdit(FoundationActor);
	check(FoundationEdit);
	UWorld* EditingWorld = FoundationEdit->GetEditWorld();
	check(EditingWorld);
			
	bool bChangesCommitted = false;
	if (IsFoundationEditDirty(FoundationEdit) && !bDiscardEdits)
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
			return;
		}

		// Validate that we indeed need to refresh instances (user can cancel the changes when prompted)
		bChangesCommitted = !IsFoundationEditDirty(FoundationEdit);

		if(bChangesCommitted)
		{
			// Sync the AssetData so that the updated instances have the latest Actor Registry Data
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.ScanPathsSynchronous({ FoundationActor->GetFoundationPackage() }, true);
		}
	}

	FScopedSlowTask SlowTask(0, LOCTEXT("EndEditFoundation", "Unloading edit foundation..."), !GetWorld()->IsGameWorld());
	SlowTask.MakeDialog();

	GEditor->SelectNone(false, true);

	// End edit non dirty child edits
	CommitChildrenFoundations(FoundationActor);

	// Try to find proper foundation to select
	AFoundationActor* ActorToSelect = FoundationActor;
	ForEachFoundationAncestors(FoundationActor, [&ActorToSelect](AFoundationActor* AncestorFoundation)
	{
		// if we find a parent editing foundation this is what we want to select
		if (AncestorFoundation->IsEditing())
		{
			ActorToSelect = AncestorFoundation;
			return false;
		}
		else // if not we go up the ancestor foundations to the highest level
		{
			ActorToSelect = AncestorFoundation;
		}
		return true;
	});

	const FString EditPackage = FoundationActor->GetFoundationPackage();

	// Remove from streaming level...
	ULevelStreamingFoundationEditor::Unload(FoundationEdit->LevelStreaming);
	FoundationEdits.Remove(FName(*EditPackage));

	// Propagate to other instances
	for (TActorIterator<AFoundationActor> FoundationIt(GetWorld()); FoundationIt; ++FoundationIt)
	{
		AFoundationActor* CurrentFoundationActor = *FoundationIt;
		if (CurrentFoundationActor->GetFoundationPackage() == EditPackage && (FoundationActor == CurrentFoundationActor || bChangesCommitted))
		{
			CurrentFoundationActor->UpdateFoundation();
		}
	}

	GEditor->SelectActor(ActorToSelect, true, true);
}

void UFoundationSubsystem::SaveFoundationAs(AFoundationActor* FoundationActor)
{
	check(CanCommitFoundation(FoundationActor));

	const FFoundationEdit* OldFoundationEdit = GetFoundationEdit(FoundationActor);
	check(OldFoundationEdit);
	UWorld* EditingWorld = OldFoundationEdit->GetEditWorld();
	check(EditingWorld);

	// Reset the level transform before saving
	OldFoundationEdit->LevelStreaming->GetLoadedLevel()->ApplyWorldOffset(-FoundationActor->GetTransform().GetLocation(), false);

	TArray<UObject*> OutObjects;
	FEditorFileUtils::SaveAssetsAs({ EditingWorld }, OutObjects);

	if (OutObjects.Num() == 0 || OutObjects[0] == EditingWorld)
	{
		UE_LOG(LogFoundation, Warning, TEXT("Failed to save foundation as new asset"));
		return;
	}

	UWorld* SavedWorld = StaticCast<UWorld*>(OutObjects[0]);
	// Discard edits and unload streaming level
	DiscardEdits();
	
	FoundationActor->SetFoundation(SavedWorld);

	LoadFoundation(FoundationActor);
	GEditor->SelectActor(FoundationActor, true, true);
}

AFoundationActor* UFoundationSubsystem::GetParentFoundation(const AActor* Actor) const
{
	check(Actor);
	const ULevel* OwningLevel = Actor->GetLevel();
	check(OwningLevel);
	return GetOwningFoundation(OwningLevel);
}
#endif

#undef LOCTEXT_NAMESPACE

