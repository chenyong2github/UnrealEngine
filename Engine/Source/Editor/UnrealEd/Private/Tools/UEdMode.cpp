// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/UEdMode.h"

#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Toolkits/BaseToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "Toolkits/ToolkitManager.h"
#include "InteractiveToolManager.h"
#include "GameFramework/Actor.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"


//////////////////////////////////
// UEdMode

UEdMode::UEdMode()
	: bPendingDeletion(false)
	, Owner(nullptr)
{
	ToolsContext = nullptr;
	ToolCommandList = MakeShareable(new FUICommandList);
}

void UEdMode::Initialize()
{
}

void UEdMode::SelectNone()
{
}

bool UEdMode::ProcessEditDelete()
{
	return false;
}

void UEdMode::Enter()
{
	// Update components for selected actors, in case the mode we just exited
	// was hijacking selection events selection and not updating components.
	Owner->GetEditorSelectionSet()->ForEachSelectedObject<AActor>([](AActor* ActorPtr)
		{
			ActorPtr->MarkComponentsRenderStateDirty();
			return true;
		});

	bPendingDeletion = false;

	ToolsContext = Owner->GetInteractiveToolsContext();
	check(ToolsContext.IsValid());

	GetToolManager()->OnToolStarted.AddUObject(this, &UEdMode::OnToolStarted);
	GetToolManager()->OnToolEnded.AddUObject(this, &UEdMode::OnToolEnded);

	// Create the settings object so that the toolkit has access to the object we are going to use at creation time
	if (SettingsClass.IsValid())
	{
		UClass* LoadedSettingsObject = SettingsClass.LoadSynchronous();
		SettingsObject = NewObject<UObject>(this, LoadedSettingsObject);
	}

	// Now that the context is ready, make the toolkit
	CreateToolkit();
	if (Toolkit.IsValid())
	{
		Toolkit->Init(Owner->GetToolkitHost(), this);
	}

	BindCommands();

	if (SettingsObject)
	{
		SettingsObject->LoadConfig();

		if (Toolkit.IsValid())
		{
			Toolkit->SetModeSettingsObject(SettingsObject);
		}
	}

	FEditorDelegates::EditorModeIDEnter.Broadcast(GetID());
}

void UEdMode::RegisterTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
{
	if (!Toolkit.IsValid())
	{
		return;
	}

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
	CommandList->MapAction(UICommand,
		FExecuteAction::CreateUObject(ToolsContext.Get(), &UEdModeInteractiveToolsContext::StartTool, ToolIdentifier),
		FCanExecuteAction::CreateWeakLambda(ToolsContext.Get(), [this, ToolIdentifier]() {
		return ShouldToolStartBeAllowed(ToolIdentifier) &&
			ToolsContext->ToolManager->CanActivateTool(EToolSide::Mouse, ToolIdentifier);
	}),
		FIsActionChecked::CreateUObject(ToolsContext.Get(), &UEdModeInteractiveToolsContext::IsToolActive, EToolSide::Mouse, ToolIdentifier),
		EUIActionRepeatMode::RepeatDisabled);

	RegisteredTools.Emplace(UICommand, ToolIdentifier);
}

bool UEdMode::ShouldToolStartBeAllowed(const FString& ToolIdentifier) const
{
	// Disallow starting tools when playing in editor or simulating.
	return !GEditor->PlayWorld && !GIsPlayInEditorWorld;
}

void UEdMode::Exit()
{
	if (SettingsObject)
	{
		SettingsObject->SaveConfig();
	}

	//Tools can live without toolkit
	for (auto& RegisteredTool : RegisteredTools)
	{
		ToolsContext->ToolManager->UnregisterToolType(RegisteredTool.Value);
	}

	if (Toolkit.IsValid())
	{
		const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
		for (auto& RegisteredTool : RegisteredTools)
		{
			CommandList->UnmapAction(RegisteredTool.Key);
		}
		Toolkit.Reset();
	}
	RegisteredTools.SetNum(0);

	GetToolManager()->OnToolStarted.RemoveAll(this);
	GetToolManager()->OnToolEnded.RemoveAll(this);

	ToolsContext = nullptr;

	FEditorDelegates::EditorModeIDExit.Broadcast(GetID());
}

bool UEdMode::UsesToolkits() const
{
	return true;
}

UWorld* UEdMode::GetWorld() const
{
	return Owner->GetWorld();
}

class FEditorModeTools* UEdMode::GetModeManager() const
{
	return Owner;
}

void UEdMode::RequestDeletion()
{
	bPendingDeletion = true;
	
	if (UsesToolkits() && Toolkit)
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
	}
}

AActor* UEdMode::GetFirstSelectedActorInstance() const
{
	return Owner->GetEditorSelectionSet()->GetTopSelectedObject<AActor>();
}

UInteractiveToolManager* UEdMode::GetToolManager() const
{
	if (ToolsContext.IsValid())
	{
		return ToolsContext->ToolManager;
	}

	return nullptr;
}

TWeakObjectPtr<UEdModeInteractiveToolsContext> UEdMode::GetInteractiveToolsContext() const
{
	return ToolsContext;
}

void UEdMode::CreateToolkit()
{
	if (!UsesToolkits())
	{
		return;
	}

	check(!Toolkit.IsValid())
	Toolkit = MakeShareable(new FModeToolkit);
}

bool UEdMode::IsSnapRotationEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->RotGridEnabled;
}
