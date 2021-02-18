// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceLevelStreaming.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstancePrivate.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Engine/LevelBounds.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "GameFramework/WorldSettings.h"

#if WITH_EDITOR
#include "LevelInstance/LevelInstanceEditorInstanceActor.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "LevelUtils.h"
#endif

ULevelStreamingLevelInstance::ULevelStreamingLevelInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LevelInstanceID(InvalidLevelInstanceID)
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);
#endif
}

ALevelInstance* ULevelStreamingLevelInstance::GetLevelInstanceActor() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		return LevelInstanceSubsystem->GetLevelInstance(LevelInstanceID);
	}

	return nullptr;
}

#if WITH_EDITOR
FBox ULevelStreamingLevelInstance::GetBounds() const
{
	check(GetLoadedLevel());
	return ALevelBounds::CalculateLevelBounds(GetLoadedLevel());
}
#endif

ULevelStreamingLevelInstance* ULevelStreamingLevelInstance::LoadInstance(ALevelInstance* LevelInstanceActor)
{
#if WITH_EDITOR
	if (!LevelInstanceActor->CheckForLoop(LevelInstanceActor->GetWorldAsset()))
	{
		UE_LOG(LogLevelInstance, Error, TEXT("Failed to load LevelInstance Actor '%s' because that would cause a loop. Run Map Check for more details."), *LevelInstanceActor->GetPathName());
		return nullptr;
	}
#endif

	bool bOutSuccess = false;

	FString ShortPackageName = FPackageName::GetShortName(LevelInstanceActor->GetWorldAsset().GetLongPackageName());
	// Build a unique and deterministic LevelInstance level instance name by using LevelInstanceID. 
	// Distinguish game from editor since we don't want to duplicate for PIE already loaded editor instances (not yet supported).
	FString Suffix = FString::Printf(TEXT("%s_LevelInstance_%08X_%d"), *ShortPackageName, LevelInstanceActor->GetLevelInstanceID(), LevelInstanceActor->GetWorld()->IsGameWorld() ? 1 : 0);
	ULevelStreamingLevelInstance* LevelStreaming = Cast<ULevelStreamingLevelInstance>(ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(LevelInstanceActor->GetWorld(), LevelInstanceActor->GetWorldAsset(), LevelInstanceActor->GetActorTransform(), bOutSuccess, Suffix, ULevelStreamingLevelInstance::StaticClass()));
	if (bOutSuccess)
	{
		LevelStreaming->LevelInstanceID = LevelInstanceActor->GetLevelInstanceID();
		
#if WITH_EDITOR
		if (!LevelInstanceActor->GetWorld()->IsPlayInEditor())
		{
			GEngine->BlockTillLevelStreamingCompleted(LevelInstanceActor->GetWorld());

			// Most of the code here is meant to allow partial support for undo/redo of LevelInstance Instance Loading:
			// by setting the objects RF_Transient and !RF_Transactional we can check when unloading if those flags
			// have been changed and figure out if we need to clear the transaction buffer or not.
			// It might not be the final solution to support Undo/Redo in LevelInstances but it handles most of the non-editing part
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

					LevelActor->PushSelectionToProxies();
					LevelActor->PushLevelInstanceEditingStateToProxies(LevelInstanceActor->IsInEditingLevelInstance());
				}
			}

			// Create special actor that will handle selection and transform
			ALevelInstanceEditorInstanceActor::Create(LevelInstanceActor, Level);
		}
#endif
		return LevelStreaming;
	}

	return nullptr;
}

void ULevelStreamingLevelInstance::UnloadInstance(ULevelStreamingLevelInstance* LevelStreaming)
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
		LevelStreaming->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>()->RemoveLevelFromWorld(LevelStreaming->GetLoadedLevel(), bResetTrans);
	}
#endif 
}

void ULevelStreamingLevelInstance::SetLoadedLevel(ULevel* Level)
{
	Super::SetLoadedLevel(Level);

	if (ULevel* NewLoadedLevel = GetLoadedLevel())
	{
		if (!NewLoadedLevel->bAlreadyMovedActors)
		{
			AWorldSettings* WorldSettings = NewLoadedLevel->GetWorldSettings();
			check(WorldSettings);
			LevelTransform.AddToTranslation(WorldSettings->LevelInstancePivotOffset);
		}
	}
