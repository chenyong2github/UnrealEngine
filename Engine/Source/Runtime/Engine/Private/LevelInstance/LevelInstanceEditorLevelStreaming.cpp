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
static FLevelInstanceID EditLevelInstanceID;
#endif

ULevelStreamingLevelInstanceEditor::ULevelStreamingLevelInstanceEditor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, LevelInstanceID(EditLevelInstanceID)
#endif
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);

	if (!IsTemplate() && !GetWorld()->IsGameWorld())
	{
		GEngine->OnLevelActorAdded().AddUObject(this, &ULevelStreamingLevelInstanceEditor::OnLevelActorAdded);
	}
#endif
}

#if WITH_EDITOR
TOptional<FFolder::FRootObject> ULevelStreamingLevelInstanceEditor::GetFolderRootObject() const
{
	if (ALevelInstance* LevelInstance = GetLevelInstanceActor())
	{
		return FFolder::FRootObject(LevelInstance);
	}

	return TOptional<FFolder::FRootObject>();
}

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
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelStreaming->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		LevelInstanceSubsystem->RemoveLevelsFromWorld({ LevelStreaming->GetLoadedLevel() });
	}
}

void ULevelStreamingLevelInstanceEditor::OnLevelActorAdded(AActor* InActor)
{
	if (InActor && InActor->GetLevel() == LoadedLevel)
	{
		InActor->PushLevelInstanceEditingStateToProxies(true);
	}
}

void ULevelStreamingLevelInstanceEditor::OnLevelLoadedChanged(ULevel* InLevel)
{
	Super::OnLevelLoadedChanged(InLevel);

	if (ULevel* NewLoadedLevel = GetLoadedLevel())
	{
		check(InLevel == NewLoadedLevel);

		// Avoid prompts for Level Instance editing
		NewLoadedLevel->bPromptWhenAddingToLevelBeforeCheckout = false;
		NewLoadedLevel->bPromptWhenAddingToLevelOutsideBounds = false;

		check(!NewLoadedLevel->bAlreadyMovedActors);
		if (AWorldSettings* WorldSettings = NewLoadedLevel->GetWorldSettings())
		{
			LevelTransform = FTransform(WorldSettings->LevelInstancePivotOffset) * LevelTransform;
		}

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			LevelInstanceSubsystem->RegisterLoadedLevelStreamingLevelInstanceEditor(this);
		}
	}
}

FBox ULevelStreamingLevelInstanceEditor::GetBounds() const
{
	check(GetLoadedLevel());
	return ALevelBounds::CalculateLevelBounds(GetLoadedLevel());
}

#endif