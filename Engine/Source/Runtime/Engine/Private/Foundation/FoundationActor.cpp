// Copyright Epic Games, Inc. All Rights Reserved.

#include "Foundation/FoundationActor.h"
#include "Foundation/FoundationSubsystem.h"
#include "Foundation/FoundationComponent.h"
#include "Foundation/FoundationEditorInstanceActor.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "FoundationPrivate.h"

#if WITH_EDITOR
#include "ActorRegistry.h"
#include "AssetData.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#endif

#define LOCTEXT_NAMESPACE "FoundationActor"

AFoundationActor::AFoundationActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, CachedFoundationID(InvalidFoundationID)
	, bGuardLoadUnload(false)
#endif
	, FoundationID(InvalidFoundationID)
{
	RootComponent = CreateDefaultSubobject<UFoundationComponent>(TEXT("Root"));
	RootComponent->Mobility = EComponentMobility::Static;
}

UFoundationSubsystem* AFoundationActor::GetFoundationSubsystem() const
{
	if (UWorld* CurrentWorld = GetWorld())
	{
		return CurrentWorld->GetSubsystem<UFoundationSubsystem>();
	}

	return nullptr;
}

void AFoundationActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsCooking() && !IsTemplate())
	{
		FGuid Guid = GetFoundationActorGuid();
		Ar << Guid;
	}
#else
	if (Ar.IsLoading())
	{
		if (IsTemplate())
		{
			check(!FoundationActorGuid.IsValid());
		}
		else if (Ar.GetPortFlags() & PPF_Duplicate)
		{
			FoundationActorGuid = FGuid::NewGuid();
		}
		else if (Ar.IsPersistent())
		{
			Ar << FoundationActorGuid;
		}
	}
#endif
}

void AFoundationActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FoundationID = FoundationSubsystem->RegisterFoundation(this);

		if (!IsRunningCommandlet())
		{
			LoadFoundation();
		}
	}
}

void AFoundationActor::PostUnregisterAllComponents()
{
	Super::PostUnregisterAllComponents();

	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		// If Foundation has already been unregistered it will have an Invalid FoundationID. Avoid processing it.
		if (!HasValidFoundationID())
		{
			return;
		}

		FoundationSubsystem->UnregisterFoundation(this);

		if (!IsRunningCommandlet())
		{
			UnloadFoundation();
		}

		// To avoid processing PostUnregisterAllComponents multiple times (BP Recompile is one use case)
		FoundationID = InvalidFoundationID;
	}

}

void AFoundationActor::LoadFoundation()
{
#if WITH_EDITOR
	if (!bGuardLoadUnload)
#endif
	{
		if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
		{
			bool bForce = false;
#if WITH_EDITOR
			// When reinstancing. Avoid reloading the level but if the package changed, force the load
			bForce = GIsReinstancing && IsLoaded() && FoundationSubsystem->GetFoundationLevel(this)->GetPackage()->FileName != FName(*GetFoundationPackage());
#endif
			FoundationSubsystem->RequestLoadFoundation(this, bForce);
		}
	}
}

void AFoundationActor::UnloadFoundation()
{
#if WITH_EDITOR
	if (!bGuardLoadUnload)
#endif
	{
		if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
		{
#if WITH_EDITOR
			check(!HasDirtyChildren());
#endif
			FoundationSubsystem->RequestUnloadFoundation(this);
		}
	}
}

bool AFoundationActor::IsFoundationPathValid() const
{
	return Foundation.GetUniqueID().IsValid();
}

bool AFoundationActor::HasValidFoundationID() const
{
	return FoundationID != InvalidFoundationID;
}

const FFoundationID& AFoundationActor::GetFoundationID() const
{
	check(HasValidFoundationID());
	return FoundationID;
}

const FGuid& AFoundationActor::GetFoundationActorGuid() const
{
#if WITH_EDITOR
	const FGuid& Guid = GetActorGuid();
#else
	const FGuid& Guid = FoundationActorGuid;
#endif
	check(IsTemplate() || Guid.IsValid());
	return Guid;
}

#if WITH_EDITOR

AActor* AFoundationActor::FindEditorInstanceActor() const
{
	AActor* FoundActor = nullptr;
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		if (FoundationSubsystem->IsLoaded(this))
		{
			FoundationSubsystem->ForEachActorInFoundation(this, [&FoundActor, this](AActor* LevelActor)
			{
				if (AFoundationEditorInstanceActor* FoundationEditorInstanceActor = Cast<AFoundationEditorInstanceActor>(LevelActor))
				{
					check(FoundationEditorInstanceActor->GetFoundationID() == GetFoundationID());
					FoundActor = FoundationEditorInstanceActor;
					return false;
				}
				return true;
			});
		}
	}

	return FoundActor;
}

AFoundationActor::FOnFoundationActorPostLoad AFoundationActor::OnFoundationActorPostLoad;

void AFoundationActor::PostLoad()
{
	Super::PostLoad();

	OnFoundationActorPostLoad.Broadcast(this);
}

void AFoundationActor::PreEditUndo()
{
	CachedFoundationID = FoundationID;
	CachedFoundation = Foundation;
	bCachedIsTemporarilyHiddenInEditor = IsTemporarilyHiddenInEditor(false);

	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PreEditUndo();
	}
}

void AFoundationActor::PostEditUndo()
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditUndo();
	}

	if (CachedFoundation != Foundation)
	{
		UpdateFoundation();
	}

	if (bCachedIsTemporarilyHiddenInEditor != IsTemporarilyHiddenInEditor(false))
	{
		if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
		{
			FoundationSubsystem->SetIsTemporarilyHiddenInEditor(this, !bCachedIsTemporarilyHiddenInEditor);
		}
	}

	// Here we want to load or unload based on our current state
	if (HasValidFoundationID() && !IsLoaded())
	{
		LoadFoundation();
	}
	else if (IsPendingKill())
	{
		// Temp restore the ID so that we can unload
		TGuardValue<FFoundationID> FoundationIDGuard(FoundationID, CachedFoundationID);
		if (IsLoaded())
		{
			UnloadFoundation();
		}
	}

	CachedFoundationID = InvalidFoundationID;
	CachedFoundation.Reset();

	if (UFoundationComponent* FoundationComponent = Cast<UFoundationComponent>(GetRootComponent()))
	{
		// Order of operations when undoing may lead to the RootComponent being undone before our actor so we need to make sure we update here and in the component when undoing
		FoundationComponent->UpdateEditorInstanceActor();
	}
}

FString AFoundationActor::GetFoundationPackage() const
{
	return Foundation.GetUniqueID().GetLongPackageName();
}

void AFoundationActor::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	if (IsPackageExternal())
	{
		static const FName NAME_FoundationPackage(TEXT("FoundationPackage"));
		FActorRegistry::SaveActorMetaData(NAME_FoundationPackage, GetFoundationPackage(), OutTags);

		static const FName NAME_FoundationTransform(TEXT("FoundationTransform"));
		FTransform FoundationTransform(GetActorRotation(), GetActorLocation());
		FActorRegistry::SaveActorMetaData(NAME_FoundationTransform, FoundationTransform, OutTags);
	}
}

void AFoundationActor::PreEditChange(FProperty* PropertyThatWillChange)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PreEditChange(PropertyThatWillChange);
	}

	if (PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(AFoundationActor, Foundation))
	{
		CachedFoundation = Foundation;
	}
}

void AFoundationActor::CheckForErrors()
{
	Super::CheckForErrors();

	TArray<TPair<FText, TSoftObjectPtr<UWorld>>> LoopInfo;
	const AFoundationActor* LoopStart = nullptr;
	if (!CheckForLoop(GetFoundation(), &LoopInfo, &LoopStart))
	{
		TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()->AddToken(FTextToken::Create(LOCTEXT("FoundationActor_Loop_CheckForErrors", "Foundation level loop found!")));
		TSoftObjectPtr<UWorld> LoopStartAsset(LoopStart->GetLevel()->GetTypedOuter<UWorld>());
		Error->AddToken(FAssetNameToken::Create(LoopStartAsset.GetLongPackageName(), FText::FromString(LoopStartAsset.GetAssetName())));
		Error->AddToken(FTextToken::Create(FText::FromString(TEXT(":"))));
		Error->AddToken(FUObjectToken::Create(LoopStart));
		for (int32 i = LoopInfo.Num() - 1; i >= 0; --i)
		{
			Error->AddToken(FTextToken::Create(LoopInfo[i].Key));
			TSoftObjectPtr<UWorld> FoundationPtr = LoopInfo[i].Value;
			Error->AddToken(FAssetNameToken::Create(FoundationPtr.GetLongPackageName(), FText::FromString(FoundationPtr.GetAssetName())));
		}

		Error->AddToken(FMapErrorToken::Create(FName(TEXT("FoundationActor_Loop_CheckForErrors"))));
	}
}

bool AFoundationActor::CheckForLoop(TSoftObjectPtr<UWorld> InFoundation, TArray<TPair<FText,TSoftObjectPtr<UWorld>>>* LoopInfo, const AFoundationActor** LoopStart) const
{
	bool bValid = true;
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FoundationSubsystem->ForEachFoundationAncestorsAndSelf(this, [&bValid, &InFoundation, LoopInfo, LoopStart, this](const AFoundationActor* FoundationActor)
		{
			// Check the level we are spawned in to detect the loop (this will handle loops caused by foundations and by regular level streaming)
			if (FoundationActor->GetLevel()->GetPackage()->FileName == FName(*InFoundation.GetLongPackageName()))
			{
				bValid = false;
				if (LoopStart)
				{
					*LoopStart = FoundationActor;
				}
			}

			if (LoopInfo)
			{
				TSoftObjectPtr<UWorld> FoundationPtr = FoundationActor == this ? InFoundation : FoundationActor->GetFoundation();
				FText FoundationName = FText::FromString(FoundationActor->GetPathName());
				FText Description = FText::Format(LOCTEXT("FoundationLoopLink", "-> Actor: {0} loads"), FoundationName);
				LoopInfo->Emplace(Description, FoundationPtr);
			}
				
			return bValid;
		});
	}

	return bValid;
}

bool AFoundationActor::CanSetValue(TSoftObjectPtr<UWorld> InFoundation, FString* Reason) const
{
	TArray<TPair<FText, TSoftObjectPtr<UWorld>>> LoopInfo;
	const AFoundationActor* LoopStart = nullptr;

	if (!CheckForLoop(InFoundation, Reason ? &LoopInfo : nullptr, Reason ? &LoopStart : nullptr))
	{
		if (Reason)
		{
			if (ensure(LoopStart))
			{
				TSoftObjectPtr<UWorld> LoopStartAsset(LoopStart->GetLevel()->GetTypedOuter<UWorld>());
				*Reason = FString::Format(TEXT("Setting Foundation to {0} would cause loop {1}:{2}\n"), { InFoundation.GetLongPackageName(), LoopStart->GetName(), LoopStartAsset.GetLongPackageName() });
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

bool AFoundationActor::SetFoundation(TSoftObjectPtr<UWorld> InFoundation)
{
	FString Reason;
	if (!CanSetValue(InFoundation, &Reason))
	{
		UE_LOG(LogFoundation, Warning, TEXT("%s"), *Reason);
		return false;
	}

	Foundation = InFoundation;
	return true;
}

void AFoundationActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditChangeProperty(PropertyChangedEvent);
	}

	if (FProperty* const PropertyThatChanged = PropertyChangedEvent.Property)
	{
		if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(AFoundationActor, Foundation))
		{
			if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
			{
				FString Reason;
				if (!CanSetValue(GetFoundation(), &Reason))
				{
					UE_LOG(LogFoundation, Warning, TEXT("%s"), *Reason);
					Foundation = CachedFoundation;
				}
				else
				{
					UpdateFoundation();
				}
				CachedFoundation.Reset();
			}
		}
	}
}

bool AFoundationActor::CanEditChange(const FProperty* InProperty) const
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

void AFoundationActor::PostEditImport()
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::PostEditImport();
	}
	UpdateFoundation();
}

bool AFoundationActor::CanDeleteSelectedActor(FText& OutReason) const
{
	if (!Super::CanDeleteSelectedActor(OutReason))
	{
		return false;
	}

	if (IsDirty())
	{
		OutReason = LOCTEXT("HasDirtyLevel", "Can't delete Foundation because it is dirty!");
		return false;
	}

	if (HasDirtyChildren())
	{
		OutReason = LOCTEXT("HasDirtryChildLevel", "Can't delete Foundation because it has dirty child foundations!");
		return false;
	}
	return true;
}

void AFoundationActor::SetIsTemporarilyHiddenInEditor(bool bIsHidden)
{
	{
		TGuardValue<bool> LoadUnloadGuard(bGuardLoadUnload, true);
		Super::SetIsTemporarilyHiddenInEditor(bIsHidden);
	}

	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FoundationSubsystem->SetIsTemporarilyHiddenInEditor(this, bIsHidden);
	}
}

void AFoundationActor::EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors)
{
	Super::EditorGetUnderlyingActors(OutUnderlyingActors);

	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FoundationSubsystem->ForEachActorInFoundation(this, [&](AActor* LevelActor)
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

void AFoundationActor::UpdateFoundation()
{
	if (HasValidFoundationID())
	{
		if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
		{
			if (IsFoundationPathValid())
			{
				const bool bForceUpdate = true;
				FoundationSubsystem->RequestLoadFoundation(this, bForceUpdate);
			}
			else if(IsLoaded())
			{
				UnloadFoundation();
			}
		}
	}
}

bool AFoundationActor::IsLoaded() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->IsLoaded(this);
	}

	return false;
}

void AFoundationActor::OnFoundationLoaded()
{
	if (!GetWorld()->IsPlayInEditor())
	{
		// Propagate bounds dirtyness up and check if we need to hide our foundation because self or ancestor is hidden
		bool bHiddenInEditor = false;
		GetFoundationSubsystem()->ForEachFoundationAncestorsAndSelf(this, [&bHiddenInEditor](const AFoundationActor* AncestorOrSelf)
		{
			AncestorOrSelf->GetLevel()->MarkLevelBoundsDirty();
			bHiddenInEditor |= AncestorOrSelf->IsTemporarilyHiddenInEditor();
			return true;
		});

		if (bHiddenInEditor)
		{
			SetIsTemporarilyHiddenInEditor(bHiddenInEditor);
		}

		UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
		if (WorldPartitionSubsystem && WorldPartitionSubsystem->IsEnabled())
		{
			WorldPartitionSubsystem->UpdateActorDesc(this);
		}
	}
}

void AFoundationActor::GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Super::GetActorLocationBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);

	// Add Level Bounds
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FBox FoundationBounds;
		if (FoundationSubsystem->GetFoundationBounds(this, FoundationBounds))
		{
			FoundationBounds.GetCenterAndExtents(Origin, BoxExtent);
		}
	}
}

FBox AFoundationActor::GetComponentsBoundingBox(bool bNonColliding, bool bIncludeFromChildActors) const
{
	FBox Box = Super::GetComponentsBoundingBox(bNonColliding, bIncludeFromChildActors);
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FBox FoundationBounds;
		if(FoundationSubsystem->GetFoundationBounds(this, FoundationBounds))
		{
			Box += FoundationBounds;
		}
	}

	return Box;
}

bool AFoundationActor::CanEdit(FText* OutReason) const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->CanEditFoundation(this, OutReason);
	}

	return false;
}

bool AFoundationActor::CanCommit(FText* OutReason) const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->CanCommitFoundation(this, OutReason);
	}

	return false;
}

bool AFoundationActor::IsEditing() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->IsEditingFoundation(this);
	}

	return false;
}

void AFoundationActor::Edit(AActor* ContextActor)
{
	UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem();
	check(FoundationSubsystem);
	FoundationSubsystem->EditFoundation(this, ContextActor);
}

void AFoundationActor::Commit()
{
	UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem();
	check(FoundationSubsystem);
	FoundationSubsystem->CommitFoundation(this);
}

void AFoundationActor::Discard()
{
	UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem();
	check(FoundationSubsystem);
	const bool bDiscardEdits = true;
	FoundationSubsystem->CommitFoundation(this, bDiscardEdits);
}

void AFoundationActor::SaveAs()
{
	UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem();
	check(FoundationSubsystem);
	FoundationSubsystem->SaveFoundationAs(this);
}

bool AFoundationActor::HasDirtyChildren() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->HasDirtyChildrenFoundations(this);
	}

	return false;
}

bool AFoundationActor::IsDirty() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->IsEditingFoundationDirty(this);
	}

	return false;
}

bool AFoundationActor::SetCurrent()
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->SetCurrent(this);
	}

	return false;
}

bool AFoundationActor::IsCurrent() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		return FoundationSubsystem->IsCurrent(this);
	}

	return false;
}

void AFoundationActor::PushSelectionToProxies()
{
	Super::PushSelectionToProxies();

	// Actors of the Foundation need to reflect the foundation actor's selected state
	if (UFoundationSubsystem* FoundationSubsystem = GetFoundationSubsystem())
	{
		FoundationSubsystem->ForEachActorInFoundation(this, [](AActor* LevelActor)
		{
			if(AFoundationEditorInstanceActor* EditorInstanceActor = Cast<AFoundationEditorInstanceActor>(LevelActor))
			{
				EditorInstanceActor->PushSelectionToProxies();
				return false;
			}
			return true;
		});
	}
}

#endif

#undef LOCTEXT_NAMESPACE
