// Copyright Epic Games, Inc. All Rights Reserved.

#include "Foundation/FoundationEditorLevelStreaming.h"

#if WITH_EDITOR
#include "Foundation/FoundationActor.h"
#include "Foundation/FoundationSubsystem.h"
#include "EditorLevelUtils.h"
#include "Editor.h"
#endif

#if WITH_EDITOR
static FFoundationID EditFoundationID = InvalidFoundationID;
#endif

ULevelStreamingFoundationEditor::ULevelStreamingFoundationEditor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, FoundationID(EditFoundationID)
#endif
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);
#endif
}

#if WITH_EDITOR
AFoundationActor* ULevelStreamingFoundationEditor::GetFoundationActor() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetWorld()->GetSubsystem<UFoundationSubsystem>())
	{
		return FoundationSubsystem->GetFoundation(FoundationID);
	}

	return nullptr;
}

ULevelStreamingFoundationEditor* ULevelStreamingFoundationEditor::Load(AFoundationActor* FoundationActor)
{
	UWorld* CurrentWorld = FoundationActor->GetWorld();

	TGuardValue<FFoundationID> GuardEditFoundationID(EditFoundationID, FoundationActor->GetFoundationID());
	ULevelStreamingFoundationEditor* LevelStreaming = Cast<ULevelStreamingFoundationEditor>(EditorLevelUtils::AddLevelToWorld(CurrentWorld, *FoundationActor->GetFoundationPackage(), ULevelStreamingFoundationEditor::StaticClass(), FoundationActor->GetTransform()));
	check(LevelStreaming);
	check(LevelStreaming->FoundationID == FoundationActor->GetFoundationID());

	GEngine->BlockTillLevelStreamingCompleted(FoundationActor->GetWorld());
	
	return LevelStreaming;
}

void ULevelStreamingFoundationEditor::Unload(ULevelStreamingFoundationEditor* LevelStreaming)
{
	// No need to clear the whole editor selection since actor of this level will be removed from the selection by: UEditorEngine::OnLevelRemovedFromWorld
	const bool bClearSelection = false;
	EditorLevelUtils::RemoveLevelFromWorld(LevelStreaming->GetLoadedLevel(), bClearSelection);
}
#endif