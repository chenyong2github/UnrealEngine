// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstanceEditorMode.h"
#include "LevelInstanceEditorModeToolkit.h"
#include "LevelInstanceEditorModeCommands.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/ILevelInstanceEditorModule.h"
#include "LevelEditorViewport.h"
#include "LevelEditorActions.h"
#include "EditorModeManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "LevelInstanceEditorMode"

FEditorModeID ULevelInstanceEditorMode::EM_LevelInstanceEditorModeId("EditMode.LevelInstance");

ULevelInstanceEditorMode::ULevelInstanceEditorMode()
	: UEdMode()
{
	Info = FEditorModeInfo(ULevelInstanceEditorMode::EM_LevelInstanceEditorModeId,
		LOCTEXT("LevelInstanceEditorModeName", "LevelInstanceEditorMode"),
		FSlateIcon(),
		false);

	bContextRestriction = true;
}

ULevelInstanceEditorMode::~ULevelInstanceEditorMode()
{
}

void ULevelInstanceEditorMode::OnPreBeginPIE(bool bSimulate)
{
	ExitModeCommand();
}

void ULevelInstanceEditorMode::UpdateEngineShowFlags()
{
	for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
	{
		if (LevelVC && LevelVC->GetWorld())
		{
			if(ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelVC->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
			{
				LevelVC->EngineShowFlags.EditingLevelInstance = !!LevelInstanceSubsystem->GetEditingLevelInstance();
			}
		}
	}
}

void ULevelInstanceEditorMode::Enter()
{
	UEdMode::Enter();

	UpdateEngineShowFlags();

	FEditorDelegates::PreBeginPIE.AddUObject(this, &ULevelInstanceEditorMode::OnPreBeginPIE);
}

void ULevelInstanceEditorMode::Exit()
{
	UEdMode::Exit();
		
	UpdateEngineShowFlags();

	bContextRestriction = true;

	FEditorDelegates::PreBeginPIE.RemoveAll(this);
}

void ULevelInstanceEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FLevelInstanceEditorModeToolkit>();
}

void ULevelInstanceEditorMode::BindCommands()
{
	UEdMode::BindCommands();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	const FLevelInstanceEditorModeCommands& Commands = FLevelInstanceEditorModeCommands::Get();

	CommandList->MapAction(
		Commands.ExitMode,
		FExecuteAction::CreateUObject(this, &ULevelInstanceEditorMode::ExitModeCommand));

	CommandList->MapAction(
		Commands.ToggleContextRestriction,
		FExecuteAction::CreateUObject(this, &ULevelInstanceEditorMode::ToggleContextRestrictionCommand),
		FCanExecuteAction(),
		FIsActionChecked::CreateUObject(this, &ULevelInstanceEditorMode::IsContextRestrictionCommandEnabled));
}

bool ULevelInstanceEditorMode::IsSelectionDisallowed(AActor* InActor, bool bInSelection) const
{
	const bool bRestrict = bContextRestriction && bInSelection;

	if (bRestrict)
	{
		if (UWorld* World = InActor->GetWorld())
		{
			if (ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(InActor))
			{
				if (LevelInstanceActor->IsEditing())
				{
					return false;
				}
			}

			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>())
			{
				ALevelInstance* EditingLevelInstance = LevelInstanceSubsystem->GetEditingLevelInstance();
				ALevelInstance* LevelInstance = LevelInstanceSubsystem->GetParentLevelInstance(InActor);

				return EditingLevelInstance != LevelInstance;
			}
		}
	}

	return bRestrict;
}

void ULevelInstanceEditorMode::ExitModeCommand()
{	
	// Ignore command when any modal window is open
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid())
	{
		return;
	}

	if (ILevelInstanceEditorModule* EditorModule = FModuleManager::GetModulePtr<ILevelInstanceEditorModule>("LevelInstanceEditor"))
	{
		EditorModule->BroadcastTryExitEditorMode();
	}
}

void ULevelInstanceEditorMode::ToggleContextRestrictionCommand()
{
	bContextRestriction = !bContextRestriction;
}

bool ULevelInstanceEditorMode::IsContextRestrictionCommandEnabled() const
{
	return bContextRestriction;
}

#undef LOCTEXT_NAMESPACE
