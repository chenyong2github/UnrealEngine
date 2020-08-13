// Copyright Epic Games, Inc. All Rights Reserved.

#include "Foundation/FoundationInstanceLevelStreaming.h"
#include "Foundation/FoundationActor.h"
#include "Foundation/FoundationPrivate.h"
#include "Foundation/FoundationSubsystem.h"
#include "Engine/LevelBounds.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "Foundation/FoundationEditorInstanceActor.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "LevelUtils.h"
#endif

ULevelStreamingFoundationInstance::ULevelStreamingFoundationInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FoundationID(InvalidFoundationID)
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);
#endif
}

AFoundationActor* ULevelStreamingFoundationInstance::GetFoundationActor() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetWorld()->GetSubsystem<UFoundationSubsystem>())
	{
		return FoundationSubsystem->GetFoundation(FoundationID);
	}

	return nullptr;
}

#if WITH_EDITOR
FBox ULevelStreamingFoundationInstance::GetBounds() const
{
	check(GetLoadedLevel());
	return ALevelBounds::CalculateLevelBounds(GetLoadedLevel());
}
#endif

ULevelStreamingFoundationInstance* ULevelStreamingFoundationInstance::LoadInstance(AFoundationActor* FoundationActor)
{
#if WITH_EDITOR
	if (!FoundationActor->CheckForLoop(FoundationActor->GetFoundation()))
	{
		UE_LOG(LogFoundation, Error, TEXT("Failed to load Foundation Actor '%s' because that would cause a loop. Run Map Check for more details."), *FoundationActor->GetPathName());
		return nullptr;
	}
#endif

	bool bOutSuccess = false;

	FString ShortPackageName = FPackageName::GetShortName(FoundationActor->GetFoundation().GetLongPackageName());
	// Build a unique and deterministic Foundation level instance name by using FoundationID. 
	// Distinguish game from editor since we don't want to duplicate for PIE already loaded editor instances (not yet supported).
	FString Suffix = FString::Printf(TEXT("%s_Foundation_%08X_%d"), *ShortPackageName, FoundationActor->GetFoundationID(), FoundationActor->GetWorld()->IsGameWorld() ? 1 : 0);
	ULevelStreamingFoundationInstance* LevelStreaming = Cast<ULevelStreamingFoundationInstance>(ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(FoundationActor->GetWorld(), FoundationActor->GetFoundation(), FoundationActor->GetActorLocation(), FoundationActor->GetActorRotation(), bOutSuccess, Suffix, ULevelStreamingFoundationInstance::StaticClass()));
	if (bOutSuccess)
	{
		LevelStreaming->FoundationID = FoundationActor->GetFoundationID();
		
#if WITH_EDITOR
		if (!FoundationActor->GetWorld()->IsPlayInEditor())
		{
			GEngine->BlockTillLevelStreamingCompleted(FoundationActor->GetWorld());

			// Most of the code here is meant to allow partial support for undo/redo of Foundation Instance Loading:
			// by setting the objects RF_Transient and !RF_Transactional we can check when unloading if those flags
			// have been changed and figure out if we need to clear the transaction buffer or not.
			// It might not be the final solution to support Undo/Redo in foundations but it handles most of the non-editing part
			ULevel* Level = LevelStreaming->GetLoadedLevel();
			check(Level);
			check(LevelStreaming->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedVisible);

			UWorld* OuterWorld = Level->GetTypedOuter<UWorld>();
			OuterWorld->ClearFlags(RF_Transactional);
			OuterWorld->SetFlags(RF_Transient);
			ResetLoaders(OuterWorld->GetPackage());

			OuterWorld->GetPackage()->ClearFlags(RF_Transactional);
			OuterWorld->GetPackage()->SetFlags(RF_Transient);

			ForEachObjectWithOuter(OuterWorld, [&](UObject* Obj)
			{
				Obj->ClearFlags(RF_Transactional);
				Obj->SetFlags(RF_Transient);
			}, true);

			for (AActor* LevelActor : Level->Actors)
			{
				if (LevelActor)
				{
					if (LevelActor->IsPackageExternal())
					{
						ResetLoaders(LevelActor->GetExternalPackage());
						LevelActor->GetPackage()->SetFlags(RF_Transient);
					}		
				}
			}

			// Create special actor that will handle selection and transform
			AFoundationEditorInstanceActor::Create(FoundationActor, Level);

			// Make sure selection is reflected after load
			FoundationActor->PushSelectionToProxies();
		}
#endif
		return LevelStreaming;
	}

	return nullptr;
}

void ULevelStreamingFoundationInstance::UnloadInstance(ULevelStreamingFoundationInstance* LevelStreaming)
{
	if (LevelStreaming->GetWorld()->IsGameWorld())
	{
		LevelStreaming->SetShouldBeLoaded(false);
		LevelStreaming->SetShouldBeVisible(false);
		LevelStreaming->SetIsRequestingUnloadAndRemoval(true);
	}
#if WITH_EDITOR
	else
	{
		// Check if we need to flush the Trans buffer...
		ULevel* LoadedLevel = LevelStreaming->GetLoadedLevel();
		UWorld* OuterWorld = LoadedLevel->GetTypedOuter<UWorld>();
		bool bResetTrans = false;
		ForEachObjectWithOuterBreakable(OuterWorld, [&bResetTrans](UObject* Obj)
		{
			if(Obj->HasAnyFlags(RF_Transactional))
			{
				bResetTrans = true;
				return false;
			}
			return true;
		}, true);

		// No need to clear the whole editor selection since actor of this level will be removed from the selection by: UEditorEngine::OnLevelRemovedFromWorld
		const bool bClearSelection = false;
		LevelStreaming->GetWorld()->GetSubsystem<UFoundationSubsystem>()->RemoveLevelFromWorld(LevelStreaming->GetLoadedLevel(), bResetTrans);
	}
#endif 
}

