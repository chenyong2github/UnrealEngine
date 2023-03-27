// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshModelingToolsCommands.h"

#include "SkeletalMesh/SkeletonEditingTool.h"

#define LOCTEXT_NAMESPACE "SkeletalMeshModelingToolsCommands"


void FSkeletalMeshModelingToolsCommands::RegisterCommands()
{
	UI_COMMAND(ToggleModelingToolsMode, "Enable Modeling Tools", "Toggles modeling tools on or off.", EUserInterfaceActionType::ToggleButton, FInputChord());
}

const FSkeletalMeshModelingToolsCommands& FSkeletalMeshModelingToolsCommands::Get()
{
	return TCommands<FSkeletalMeshModelingToolsCommands>::Get();
}

FSkeletalMeshModelingToolsActionCommands::FSkeletalMeshModelingToolsActionCommands() : 
	TInteractiveToolCommands<FSkeletalMeshModelingToolsActionCommands>(
		"SeletalMeshModelingToolsEditMode", // Context name for fast lookup
		NSLOCTEXT("Contexts", "SeletalMeshModelingToolsEditMode", "Skeletal Mesh Modeling Tools - Shared Shortcuts"), // Localized context name for displaying
		NAME_None, // Parent
		FAppStyle::GetAppStyleSetName() // Icon Style Set
	)
{
}

void FSkeletalMeshModelingToolsActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{}

void FSkeletalMeshModelingToolsActionCommands::RegisterAllToolActions()
{
	FSkeletonEditingToolActionCommands::Register();
}

void FSkeletalMeshModelingToolsActionCommands::UnregisterAllToolActions()
{
	FSkeletonEditingToolActionCommands::Unregister();
}

void FSkeletalMeshModelingToolsActionCommands::UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind)
{
#define UPDATE_BINDING(CommandsType)  if (!bUnbind) CommandsType::Get().BindCommandsForCurrentTool(UICommandList, Tool); else CommandsType::Get().UnbindActiveCommands(UICommandList);

	if (ExactCast<USkeletonEditingTool>(Tool) != nullptr)
	{
		UPDATE_BINDING(FSkeletonEditingToolActionCommands);
	}
}

#define DEFINE_TOOL_ACTION_COMMANDS(CommandsClassName, ContextNameString, SettingsDialogString, ToolClassName ) \
CommandsClassName::CommandsClassName() : TInteractiveToolCommands<CommandsClassName>( \
ContextNameString, NSLOCTEXT("Contexts", ContextNameString, SettingsDialogString), NAME_None, FAppStyle::GetAppStyleSetName()) {} \
void CommandsClassName::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) \
{\
ToolCDOs.Add(GetMutableDefault<ToolClassName>()); \
}

DEFINE_TOOL_ACTION_COMMANDS(FSkeletonEditingToolActionCommands, "SkeletalMeshModelingToolsSkeletonEditing", "Skeletal Mesh Modeling Tools - Skeleton Editing Tool", USkeletonEditingTool);

#undef LOCTEXT_NAMESPACE
