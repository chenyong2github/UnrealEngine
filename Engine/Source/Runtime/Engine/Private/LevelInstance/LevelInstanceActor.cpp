// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceComponent.h"
#include "LevelInstance/LevelInstanceEditorInstanceActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "LevelInstancePrivate.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#endif

#define LOCTEXT_NAMESPACE "LevelInstanceActor"

ALevelInstance::ALevelInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, bGuardLoadUnload(false)
#endif
{
	RootComponent = CreateDefaultSubobject<ULevelInstanceComponent>(TEXT("Root"));
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	DesiredRuntimeBehavior = ELevelInstanceRuntimeBehavior::Partitioned;
#endif
}

ULevelInstanceSubsystem* ALevelInstance::GetLevelInstanceSubsystem() const
{
	if (UWorld* CurrentWorld = GetWorld())
	{
		return CurrentWorld->GetSubsystem<ULevelInstanceSubsystem>();
	}

	return nullptr;
}

void ALevelInstance::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsCooking() && !IsTemplate())
	{
		FGuid Guid = GetLevelInstanceActorGuid();
		Ar << Guid;
	}
#else
	if (Ar.IsLoading())
	{
		if (IsTemplate())
		{
			check(!LevelInstanceActorGuid.IsValid());
		}
		else if (Ar.GetPortFlags() & PPF_Duplicate)
		{
			LevelInstanceActorGuid = FGuid::NewGuid();
		}
		else if (Ar.IsPersistent())
		{
			Ar << LevelInstanceActorGuid;
			check(LevelInstanceActorGuid.IsValid());
		}
	}
#endif
}

void ALevelInstance::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
#if !WITH_EDITOR
		// If the level instance was spawned, not loaded
		if (!LevelInstanceActorGuid.IsValid())
		{
			LevelInstanceActorGuid = FGuid::NewGuid();
		}
#endif
		LevelInstanceID = LevelInstanceSubsystem->RegisterLevelInstance(this);

		LoadLevelInstance();

#if WITH_EDITOR
		// Make sure transformation is up to date after registration as its possible LevelInstance actor can get unregistered when editing properties
		// through Details panel. In this case the ULevelInstanceComponent might not be able to update the ALevelInstanceEditorInstanceActor transform.
		Cast<ULevelInstanceComponent>(GetRootComponent())->UpdateEditorInstanceActor();
#endif
	}
}

void ALevelInstance::PostUnregisterAllComponents()
{
	Super::PostUnregisterAllComponents();

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		// If LevelInstance has already been unregistered it will have an Invalid LevelInstanceID. Avoid processing it.
		if (!HasValidLevelInstanceID())
		{
			return;
		}

		LevelInstanceSubsystem->UnregisterLevelInstance(this);

		UnloadLevelInstance();

		// To avoid processing PostUnregisterAllComponents multiple times (BP Recompile is one use case)
		LevelInstanceID = FLevelInstanceID();
	}

}

bool ALevelInstance::SupportsLoading() const
{
#if WITH_EDITOR
	return !bGuardLoadUnload && !bIsEditorPreviewActor;
#else
	return true;
#endif
}

void ALevelInstance::LoadLevelInstance()
{
	if(SupportsLoading())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			bool bForce = false;
#if WITH_EDITOR
			// When reinstancing or world wasn't ticked between changes. Avoid reloading the level but if the package changed, force the load
			bForce = IsLoaded() && LevelInstanceSubsystem->GetLevelInstanceLevel(this)->GetPackage()->GetLoadedPath() != FPackagePath::FromPackageNameChecked(GetWorldAssetPackage());
#endif
			LevelInstanceSubsystem->RequestLoadLevelInstance(this, bForce);
		}
	}
}

void ALevelInstance::UnloadLevelInstance()
{
	if(SupportsLoading())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
#if WITH_EDITOR
			check(!HasDirtyChildren());
#endif
			LevelInstanceSubsystem->RequestUnloadLevelInstance(this);
		}
	}
}

const TSoftObjectPtr<UWorld>& ALevelInstance::GetWorldAsset() const
{
#if WITH_EDITORONLY_DATA
	return WorldAsset;
#else
	return CookedWorldAsset;
#endif
}

bool ALevelInstance::IsLevelInstancePathValid() const
{
	return GetWorldAsset().GetUniqueID().IsValid();
}

bool ALevelInstance::HasValidLevelInstanceID() const
{
	return LevelInstanceID.IsValid();
}

const FLevelInstanceID& ALevelInstance::GetLevelInstanceID() const
{
	check(HasValidLevelInstanceID());
	return LevelInstanceID;
}

const FGuid& ALevelInstance::GetLevelInstanceActorGuid() const
{
#if WITH_EDITOR
	const FGuid& Guid = GetActorGuid();
#else
	const FGuid& Guid = LevelInstanceActorGuid;
#endif
	check(IsTemplate() || Guid.IsValid());
	return Guid;
}

#if WITH_EDITOR

TUniquePtr<FWorldPartitionActorDesc> ALevelInstance::CreateClassActorDesc() const
{
	return TUniquePtr<FWorldPartitionActorDesc>(new FLevelInstanceActorDesc());
}

AActor* ALevelInstance::FindEditorInstanceActor() const
{
	AActor* FoundActor = nullptr;
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		if (LevelInstanceSubsystem->IsLoaded(this))
		{
			LevelInstanceSubsystem->ForEachActorInLevelInstance(this, [&FoundActor, this](AActor* LevelActor)
			{
				if (ALevelInstanceEditorInstanceActor* LevelInstanceEditorInstanceActor = Cast<ALevelInstanceEditorInstanceActor>(LevelActor))
				{
					check(LevelInstanceEditorInstanceActor->GetLevelInstanceID() == GetLevelInstanceID());
					FoundActor = LevelInstanceEditorInstanceActor;
					return false;
				}
				return true;
			});
		}
	}

	return FoundActor;
}

ALevelInstance::FOnLevelInstanceActorPostLoad ALevelInstance::OnLevelInstanceActorPostLoad;

void ALevelInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (ULevel::GetIsLevelPartitionedFromPackage(*WorldAsset.GetLongPackageName()))
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("LevelInstance doesn't support partitioned world %s"), *WorldAsset.GetLongPackageName());
		WorldAsset.Reset();
	}
#endif
		
#if WITH_EDITORONLY_DATA
	if (IsRunningCookCommandlet() && SupportsLoading())
	{
		CookedWorldAsset = WorldAsset;
	}
#endif

	OnLevelInstanceActorPostLoad.Broadcast(this);
}

void ALevelInstance::PreEditUndo()
{
	CachedLevelInstanceID = LevelInstanceID;
	CachedWorldAsset = WorldAsset;
	bCachedIsTemporarilyHiddenInEditor = IsTemporarilyHiddenInEditor(false);

	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PreEditUndo();
	}
}

void ALevelInstance::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditUndo(TransactionAnnotation);
	}

	PostEditUndoInternal();
}

void ALevelInstance::PostEditUndo()
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditUndo();
	}

	PostEditUndoInternal();
}

void ALevelInstance::PostEditUndoInternal()
{
	if (CachedWorldAsset != WorldAsset)
	{
		OnWorldAssetChanged();
	}

	if (bCachedIsTemporarilyHiddenInEditor != IsTemporarilyHiddenInEditor(false))
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			LevelInstanceSubsystem->SetIsTemporarilyHiddenInEditor(this, !bCachedIsTemporarilyHiddenInEditor);
		}
	}

	// Here we want to load or unload based on our current state
	if (HasValidLevelInstanceID() && !IsLoaded())
	{
		LoadLevelInstance();
	}
	else if (!IsValidChecked(this))
	{
		// Temp restore the ID so that we can unload
		TGuardValue<FLevelInstanceID> LevelInstanceIDGuard(LevelInstanceID, CachedLevelInstanceID);
		if (IsLoaded())
		{
			UnloadLevelInstance();
		}
	}

	CachedLevelInstanceID = FLevelInstanceID();
	CachedWorldAsset.Reset();

	if (ULevelInstanceComponent* LevelInstanceComponent = Cast<ULevelInstanceComponent>(GetRootComponent()))
	{
		// Order of operations when undoing may lead to the RootComponent being undone before our actor so we need to make sure we update here and in the component when undoing
		LevelInstanceComponent->UpdateEditorInstanceActor();
	}
}

FString ALevelInstance::GetWorldAssetPackage() const
{
	return GetWorldAsset().GetUniqueID().GetLongPackageName();
}

void ALevelInstance::PreEditChange(FProperty* PropertyThatWillChange)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PreEditChange(PropertyThatWillChange);
	}

	if (PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(ALevelInstance, WorldAsset))
	{
		CachedWorldAsset = WorldAsset;
	}
}

void ALevelInstance::CheckForErrors()
{
	Super::CheckForErrors();

	TArray<TPair<FText, TSoftObjectPtr<UWorld>>> LoopInfo;
	const ALevelInstance* LoopStart = nullptr;
	if (!CheckForLoop(GetWorldAsset(), &LoopInfo, &LoopStart))
	{
		TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()->AddToken(FTextToken::Create(LOCTEXT("LevelInstanceActor_Loop_CheckForErrors", "LevelInstance level loop found!")));
		TSoftObjectPtr<UWorld> LoopStartAsset(LoopStart->GetLevel()->GetTypedOuter<UWorld>());
		Error->AddToken(FAssetNameToken::Create(LoopStartAsset.GetLongPackageName(), FText::FromString(LoopStartAsset.GetAssetName())));
		Error->AddToken(FTextToken::Create(FText::FromString(TEXT(":"))));
		Error->AddToken(FUObjectToken::Create(LoopStart));
		for (int32 i = LoopInfo.Num() - 1; i >= 0; --i)
		{
			Error->AddToken(FTextToken::Create(LoopInfo[i].Key));
			TSoftObjectPtr<UWorld> LevelInstancePtr = LoopInfo[i].Value;
			Error->AddToken(FAssetNameToken::Create(LevelInstancePtr.GetLongPackageName(), FText::FromString(LevelInstancePtr.GetAssetName())));
		}

		Error->AddToken(FMapErrorToken::Create(FName(TEXT("LevelInstanceActor_Loop_CheckForErrors"))));
	}

	FPackagePath WorldAssetPath;
	if (!FPackagePath::TryFromPackageName(GetWorldAssetPackage(), WorldAssetPath) || !FPackageName::DoesPackageExist(WorldAssetPath))
	{
		TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
			->AddToken(FTextToken::Create(LOCTEXT("LevelInstanceActor_InvalidPackage", "LevelInstance actor")))
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(TEXT("refers to an invalid asset:"))))
			->AddToken(FAssetNameToken::Create(GetWorldAsset().GetLongPackageName(), FText::FromString(GetWorldAsset().GetLongPackageName())))
			->AddToken(FMapErrorToken::Create(FName(TEXT("LevelInstanceActor_InvalidPackage_CheckForErrors"))));
	}
}

bool ALevelInstance::CheckForLoop(TSoftObjectPtr<UWorld> InLevelInstance, TArray<TPair<FText,TSoftObjectPtr<UWorld>>>* LoopInfo, const ALevelInstance** LoopStart) const
{
	bool bValid = true;
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(this, [&bValid, &InLevelInstance, LoopInfo, LoopStart, this](const ALevelInstance* LevelInstanceActor)
		{
			FName LongPackageName(*InLevelInstance.GetLongPackageName());
			// Check to exclude NAME_None since Preview Levels are in the transient package
			// Check the level we are spawned in to detect the loop (this will handle loops caused by LevelInstances and by regular level streaming)
			if (LongPackageName != NAME_None && LevelInstanceActor->GetLevel()->GetPackage()->GetLoadedPath() == FPackagePath::FromPackageNameChecked(LongPackageName))
			{
				bValid = false;
				if (LoopStart)
				{
					*LoopStart = LevelInstanceActor;
				}
			}

			if (LoopInfo)
			{
				TSoftObjectPtr<UWorld> LevelInstancePtr = LevelInstanceActor == this ? InLevelInstance : LevelInstanceActor->GetWorldAsset();
				FText LevelInstanceName = FText::FromString(LevelInstanceActor->GetPathName());
				FText Description = FText::Format(LOCTEXT("LevelInstanceLoopLink", "-> Actor: {0} loads"), LevelInstanceName);
				LoopInfo->Emplace(Description, LevelInstancePtr);
			}
				
			return bValid;
		});
	}

	return bValid;
}

bool ALevelInstance::CanSetValue(TSoftObjectPtr<UWorld> InLevelInstance, FString* Reason) const
{
	// Set to null is valid
	if (InLevelInstance.IsNull())
	{
		return true;
	}

	FString PackageName;
	if (!FPackageName::DoesPackageExist(InLevelInstance.GetLongPackageName()))
	{
		if (Reason)
		{
			*Reason = FString::Format(TEXT("Attempting to set Level Instance to package {0} which does not exist. Ensure the level was saved before attepting to set the level instance world asset."), { InLevelInstance.GetLongPackageName() });
		}
		return false;
	}

	TArray<TPair<FText, TSoftObjectPtr<UWorld>>> LoopInfo;
	const ALevelInstance* LoopStart = nullptr;

	if (ULevel::GetIsLevelPartitionedFromPackage(*InLevelInstance.GetLongPackageName()))
	{
		if (Reason)
		{
			*Reason = FString::Format(TEXT("LevelInstance doesn't support partitioned world {0}\n"), { InLevelInstance.GetLongPackageName() });
		}

		return false;
	}

	if (!CheckForLoop(InLevelInstance, Reason ? &LoopInfo : nullptr, Reason ? &LoopStart : nullptr))
	{
		if (Reason)
		{
			if (ensure(LoopStart))
			{
				TSoftObjectPtr<UWorld> LoopStartAsset(LoopStart->GetLevel()->GetTypedOuter<UWorld>());
				*Reason = FString::Format(TEXT("Setting LevelInstance to {0} would cause loop {1}:{2}\n"), { InLevelInstance.GetLongPackageName(), LoopStart->GetName(), LoopStartAsset.GetLongPackageName() });
				for (int32 i = LoopInfo.Num() - 1; i >= 0; --i)
				{
					Reason->Append(FString::Format(TEXT("{0} {1}\n"), { *LoopInfo[i].Key.ToString(), *LoopInfo[i].Value.GetLongPackageName() }));
				}
			}
		}

		return false;
	}

	return true;
}

bool ALevelInstance::SetWorldAsset(TSoftObjectPtr<UWorld> InWorldAsset)
{
	FString Reason;
	if (!CanSetValue(InWorldAsset, &Reason))
	{
		UE_LOG(LogLevelInstance, Warning, TEXT("%s"), *Reason);
		return false;
	}

	WorldAsset = InWorldAsset;
	return true;
}

void ALevelInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditChangeProperty(PropertyChangedEvent);
	}

	if (FProperty* const PropertyThatChanged = PropertyChangedEvent.Property)
	{
		if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ALevelInstance, WorldAsset))
		{
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
			{
				FString Reason;
				if (!CanSetValue(GetWorldAsset(), &Reason))
				{
					UE_LOG(LogLevelInstance, Warning, TEXT("%s"), *Reason);
					WorldAsset = CachedWorldAsset;
				}
				else
				{
					OnWorldAssetChanged();
				}
				CachedWorldAsset.Reset();
			}
		}
	}
}

bool ALevelInstance::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (IsEditing())
	{
		return false;
	}

	if (HasDirtyChildren())
	{
		return false;
	}

	return true;
}

void ALevelInstance::PostEditImport()
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditImport();
	}
	UpdateFromLevel();
}

bool ALevelInstance::CanDeleteSelectedActor(FText& OutReason) const
{
	if (!Super::CanDeleteSelectedActor(OutReason))
	{
		return false;
	}

	if (IsEditing())
	{
		OutReason = LOCTEXT("HasEditingLevel", "Can't delete LevelInstance because it is editing!");
		return false;
	}

	if (HasChildEdit())
	{
		OutReason = LOCTEXT("HasEditingChildLevel", "Can't delete LevelInstance because it has editing child LevelInstances!");
		return false;
	}
	return true;
}

void ALevelInstance::SetIsTemporarilyHiddenInEditor(bool bIsHidden)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::SetIsTemporarilyHiddenInEditor(bIsHidden);
	}

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceSubsystem->SetIsTemporarilyHiddenInEditor(this, bIsHidden);
	}
}

bool ALevelInstance::SetIsHiddenEdLayer(bool bIsHiddenEdLayer)
{
	bool bHasChanged = false;
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		bHasChanged = Super::SetIsHiddenEdLayer(bIsHiddenEdLayer);
	}
	if (bHasChanged)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			LevelInstanceSubsystem->SetIsHiddenEdLayer(this, bIsHiddenEdLayer);
		}
	}
	return bHasChanged;
}

void ALevelInstance::EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors) const
{
	Super::EditorGetUnderlyingActors(OutUnderlyingActors);

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceSubsystem->ForEachActorInLevelInstance(this, [&](AActor* LevelActor)
		{
			bool bAlreadySet = false;
			OutUnderlyingActors.Add(LevelActor, &bAlreadySet);
			if (!bAlreadySet)
			{
				LevelActor->EditorGetUnderlyingActors(OutUnderlyingActors);
			}
			return true;
		});
	}
}

void ALevelInstance::UpdateFromLevel()
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			if (IsLevelInstancePathValid() && SupportsLoading())
			{
				const bool bForceUpdate = true;
				LevelInstanceSubsystem->RequestLoadLevelInstance(this, bForceUpdate);
			}
			else if(IsLoaded())
			{
				UnloadLevelInstance();
			}
		}
	}
}

bool ALevelInstance::IsLoaded() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->IsLoaded(this);
	}

	return false;
}

void ALevelInstance::OnLevelInstanceLoaded()
{
	if (!GetWorld()->IsGameWorld())
	{
		// Propagate bounds dirtyness up and check if we need to hide our LevelInstance because self or ancestor is hidden
		bool bHiddenInEditor = false;
		GetLevelInstanceSubsystem()->ForEachLevelInstanceAncestorsAndSelf(this, [&bHiddenInEditor](const ALevelInstance* AncestorOrSelf)
		{
			AncestorOrSelf->GetLevel()->MarkLevelBoundsDirty();
			bHiddenInEditor |= AncestorOrSelf->IsTemporarilyHiddenInEditor();
			return true;
		});

		if (bHiddenInEditor)
		{
			SetIsTemporarilyHiddenInEditor(bHiddenInEditor);
		}
	}
}

FBox ALevelInstance::GetStreamingBounds() const
{
	// Add Level Bounds
	if (SupportsLoading())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			FBox LevelInstanceBounds;
			if (LevelInstanceSubsystem->GetLevelInstanceBounds(this, LevelInstanceBounds))
			{
				return LevelInstanceBounds;
			}
		}
	}

	return Super::GetStreamingBounds();
}

bool ALevelInstance::IsLockLocation() const
{
	return Super::IsLockLocation() || IsEditing() || HasChildEdit();
}

FBox ALevelInstance::GetComponentsBoundingBox(bool bNonColliding, bool bIncludeFromChildActors) const
{
	FBox Box = Super::GetComponentsBoundingBox(bNonColliding, bIncludeFromChildActors);
	
	// Add Level Bounds
	if (SupportsLoading())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			FBox LevelInstanceBounds;
			if (LevelInstanceSubsystem->GetLevelInstanceBounds(this, LevelInstanceBounds))
			{
				Box += LevelInstanceBounds;
			}
		}
	}

	return Box;
}

bool ALevelInstance::CanEdit(FText* OutReason) const
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			return LevelInstanceSubsystem->CanEditLevelInstance(this, OutReason);
		}
	}

	return false;
}

bool ALevelInstance::CanCommit(FText* OutReason) const
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			return LevelInstanceSubsystem->CanCommitLevelInstance(this, OutReason);
		}
	}

	return false;
}

bool ALevelInstance::CanDiscard(FText* OutReason) const
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			return LevelInstanceSubsystem->CanDiscardLevelInstance(this, OutReason);
		}
	}

	return false;
}

bool ALevelInstance::IsEditing() const
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			return LevelInstanceSubsystem->IsEditingLevelInstance(this);
		}
	}
	return false;
}

ULevel* ALevelInstance::GetLoadedLevel() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->GetLevelInstanceLevel(this);
	}
	return nullptr;
}

bool ALevelInstance::HasChildEdit() const
{
	if (HasValidLevelInstanceID())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
		{
			return LevelInstanceSubsystem->HasChildEdit(this);
		}
	}

	return false;
}

void ALevelInstance::Edit(AActor* ContextActor)
{
	ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem();
	check(LevelInstanceSubsystem);
	LevelInstanceSubsystem->EditLevelInstance(this, ContextActor);
}

void ALevelInstance::Commit()
{
	ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem();
	check(LevelInstanceSubsystem);
	LevelInstanceSubsystem->CommitLevelInstance(this);
}

void ALevelInstance::Discard()
{
	ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem();
	check(LevelInstanceSubsystem);
	const bool bDiscardEdits = true;
	LevelInstanceSubsystem->CommitLevelInstance(this, bDiscardEdits);
}

bool ALevelInstance::HasDirtyChildren() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->HasDirtyChildrenLevelInstances(this);
	}

	return false;
}

bool ALevelInstance::IsDirty() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->IsEditingLevelInstanceDirty(this);
	}

	return false;
}

bool ALevelInstance::SetCurrent()
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->SetCurrent(this);
	}

	return false;
}

bool ALevelInstance::IsCurrent() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->IsCurrent(this);
	}

	return false;
}

void ALevelInstance::PushSelectionToProxies()
{
	Super::PushSelectionToProxies();

	// Actors of the LevelInstance need to reflect the LevelInstance actor's selected state
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceSubsystem->ForEachActorInLevelInstance(this, [](AActor* LevelActor)
		{
			if(ALevelInstanceEditorInstanceActor* EditorInstanceActor = Cast<ALevelInstanceEditorInstanceActor>(LevelActor))
			{
				EditorInstanceActor->PushSelectionToProxies();
				return false;
			}
			return true;
		});
	}
}

void ALevelInstance::PushLevelInstanceEditingStateToProxies(bool bInEditingState)
{
	Super::PushLevelInstanceEditingStateToProxies(bInEditingState);

	// Actors of the LevelInstance need to reflect the LevelInstance actor's Editing state
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceSubsystem->ForEachActorInLevelInstance(this, [bInEditingState](AActor* LevelActor)
			{
				LevelActor->PushLevelInstanceEditingStateToProxies(bInEditingState);
				return true;
			});
	}
}

#endif

#undef LOCTEXT_NAMESPACE
