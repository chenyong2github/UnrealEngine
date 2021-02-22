// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceEditorLevelStreaming.h"

#if WITH_EDITOR
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceEditorPivotActor.h"
#include "EditorLevelUtils.h"
#include "Editor.h"
#include "Engine/LevelBounds.h"
#include "GameFramework/WorldSettings.h"
#endif

#if WITH_EDITOR
static FLevelInstanceID EditLevelInstanceID = InvalidLevelInstanceID;
#endif

ULevelStreamingLevelInstanceEditor::ULevelStreamingLevelInstanceEditor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, LevelInstanceID(EditLevelInstanceID)
#endif
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);

	if (!IsTemplate() && !GetWorld()->IsPlayInEditor())
	{
		GEngine->OnLevelActorAdded().AddUObject(this, &ULevelStreamingLevelInstanceEditor::OnLevelActorAdded);
	}
#endif
}

#if WITH_EDITOR
ALevelInstance* ULevelStreamingLevelInstanceEditor::GetLevelInstanceActor() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		return LevelInstanceSubsystem->GetLevelInstance(LevelInstanceID);
	}

	return nullptr;
}

ULevelStreamingLevelInstanceEditor* ULevelStreamingLevelInstanceEditor::Load(ALevelInstance* LevelInstanceActor)
{
	UWorld* CurrentWorld = LevelInstanceActor->GetWorld();

	TGuardValue<FLevelInstanceID> GuardEditLevelInstanceID(EditLevelInstanceID, LevelInstanceActor->GetLevelInstanceID());
	if (ULevelStreamingLevelInstanceEditor* LevelStreaming = Cast<ULevelStreamingLevelInstanceEditor>(EditorLevelUtils::AddLevelToWorld(CurrentWorld, *LevelInstanceActor->GetWorldAssetPackage(), ULevelStreamingLevelInstanceEditor::StaticClass(), LevelInstanceActor->GetTransform())))
	{
		check(LevelStreaming);
		check(LevelStreaming->LevelInstanceID == LevelInstanceActor->GetLevelInstanceID());

		GEngine->BlockTillLevelStreamingCompleted(LevelInstanceActor->GetWorld());

		// Create special actor that will handle changing the pivot of this level
		ALevelInstancePivot::Create(LevelInstanceActor, LevelStreaming);

		return LevelStreaming;
	}

	return nullptr;
}

void ULevelStreamingLevelInstanceEditor::Unload(ULevelStreamingLevelInstanceEditor* LevelStreaming)
{
	// No need to clear the whole editor selection since actor of this level will be removed from the selection by: UEditorEngine::OnLevelRemovedFromWorld
	const bool bClearSelection = false;
	EditorLevelUtils::RemoveLevelFromWorld(LevelStreaming->GetLoadedLevel(), bClearSelection);
}

void ULevelStreamingLevelInstanceEditor::OnLevelActorAdded(AActor* InActor)
{
	if (InActor && InActor->GetLevel() == LoadedLevel)
	{
		InActor->PushLevelInstanceEditingStateToProxies(true);
	}
}

void ULevelStreamingLevelInstanceEditor::SetLoadedLevel(ULevel* Level)
{
	Super::SetLoadedLevel(Level);

	if (ULevel* NewLoadedLevel = GetLoadedLevel())
	{
		check(!NewLoadedLevel->bAlreadyMovedActors);
		if (AWorldSettings* WorldSettings = NewLoadedLevel->GetWorldSettings())
		{
			LevelTransform.AddToTranslation(WorldSettings->LevelInstancePivotOffset);
		}
	}
}

FBox ULevelStreamingLevelInstanceEditor::GetBounds() const
{
	check(GetLoadedLevel());
	return ALevelBounds::CalculateLevelBounds(GetLoadedLevel());
}

#endif