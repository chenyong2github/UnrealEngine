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
#endif

#define LOCTEXT_NAMESPACE "LevelInstanceActor"

ALevelInstance::ALevelInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, CachedLevelInstanceID(InvalidLevelInstanceID)
	, bGuardLoadUnload(false)
#endif
	, LevelInstanceID(InvalidLevelInstanceID)
{
	RootComponent = CreateDefaultSubobject<ULevelInstanceComponent>(TEXT("Root"));
	RootComponent->Mobility = EComponentMobility::Static;
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
		}
	}
#endif
}

void ALevelInstance::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		LevelInstanceID = LevelInstanceSubsystem->RegisterLevelInstance(this);

		if (!IsRunningCommandlet())
		{
			LoadLevelInstance();
		}
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

		if (!IsRunningCommandlet())
		{
			UnloadLevelInstance();
		}

		// To avoid processing PostUnregisterAllComponents multiple times (BP Recompile is one use case)
		LevelInstanceID = InvalidLevelInstanceID;
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

bool ALevelInstance::IsLevelInstancePathValid() const
{
	return WorldAsset.GetUniqueID().IsValid();
}

bool ALevelInstance::HasValidLevelInstanceID() const
{
	return LevelInstanceID != InvalidLevelInstanceID;
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

void ALevelInstance::PostEditUndo()
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditUndo();
	}

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
	else if (IsPendingKill())
	{
		// Temp restore the ID so that we can unload
		TGuardValue<FLevelInstanceID> LevelInstanceIDGuard(LevelInstanceID, CachedLevelInstanceID);
		if (IsLoaded())
		{
			UnloadLevelInstance();
		}
	}

	CachedLevelInstanceID = InvalidLevelInstanceID;
	CachedWorldAsset.Reset();

	if (ULevelInstanceComponent* LevelInstanceComponent = Cast<ULevelInstanceComponent>(GetRootComponent()))
	{
		// Order of operations when undoing may lead to the RootComponent being undone before our actor so we need to make sure we update here and in the component when undoing
		LevelInstanceComponent->UpdateEditorInstanceActor();
	}
}

FString ALevelInstance::GetWorldAssetPackage() const
{
	return WorldAsset.GetUniqueID().GetLongPackageName();
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
	TArray<TPair<FText, TSoftObjectPtr<UWorld>>> LoopInfo;
	const ALevelInstance* LoopStart = nullptr;

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
	UpdateLevelInstance();
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

	if (HasEditingChildren())
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

void ALevelInstance::EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors)
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

void ALevelInstance::UpdateLevelInstance()
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
	if (!GetWorld()->IsPlayInEditor())
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

void ALevelInstance::GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Super::GetActorLocationBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);

	// Add Level Bounds
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		FBox LevelInstanceBounds;
		if (LevelInstanceSubsystem->GetLevelInstanceBounds(this, LevelInstanceBounds))
		{
			LevelInstanceBounds.GetCenterAndExtents(Origin, BoxExtent);
		}
	}
}

FBox ALevelInstance::GetComponentsBoundingBox(bool bNonColliding, bool bIncludeFromChildActors) const
{
	FBox Box = Super::GetComponentsBoundingBox(bNonColliding, bIncludeFromChildActors);
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		FBox LevelInstanceBounds;
		if(LevelInstanceSubsystem->GetLevelInstanceBounds(this, LevelInstanceBounds))
		{
			Box += LevelInstanceBounds;
		}
	}

	return Box;
}

bool ALevelInstance::CanEdit(FText* OutReason) const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->CanEditLevelInstance(this, OutReason);
	}

	return false;
}

bool ALevelInstance::CanCommit(FText* OutReason) const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->CanCommitLevelInstance(this, OutReason);
	}

	return false;
}

bool ALevelInstance::IsEditing() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->IsEditingLevelInstance(this);
	}

	return false;
}

bool ALevelInstance::HasEditingChildren() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem())
	{
		return LevelInstanceSubsystem->HasEditingChildrenLevelInstances(this);
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

void ALevelInstance::SaveAs()
{
	ULevelInstanceSubsystem* LevelInstanceSubsystem = GetLevelInstanceSubsystem();
	check(LevelInstanceSubsystem);
	LevelInstanceSubsystem->SaveLevelInstanceAs(this);
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
