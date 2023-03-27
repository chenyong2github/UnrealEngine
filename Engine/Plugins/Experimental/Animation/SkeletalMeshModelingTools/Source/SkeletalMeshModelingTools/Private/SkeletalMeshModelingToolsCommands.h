// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"
#include "Tools/InteractiveToolsCommands.h"


class FSkeletalMeshModelingToolsCommands : public TCommands<FSkeletalMeshModelingToolsCommands>
{
public:
	FSkeletalMeshModelingToolsCommands()
	    : TCommands<FSkeletalMeshModelingToolsCommands>(
	    	TEXT("SkeletalMeshModelingTools"),
	    	NSLOCTEXT("Contexts", "SkeletalMeshModelingToolsCommands", "Skeletal Mesh Modeling Tools"),
	    	NAME_None,
	    	FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;
	static const FSkeletalMeshModelingToolsCommands& Get();

	// Modeling tools commands
	TSharedPtr<FUICommandInfo> ToggleModelingToolsMode;
};

class FSkeletalMeshModelingToolsActionCommands : public TInteractiveToolCommands<FSkeletalMeshModelingToolsActionCommands>
{
public:
	FSkeletalMeshModelingToolsActionCommands();

	// TInteractiveToolCommands
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
	
	static void RegisterAllToolActions();
	static void UnregisterAllToolActions();
	static void UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind = false);
};

#define DECLARE_TOOL_ACTION_COMMANDS(CommandsClassName) \
class CommandsClassName : public TInteractiveToolCommands<CommandsClassName> \
{\
public:\
CommandsClassName();\
virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;\
};\

DECLARE_TOOL_ACTION_COMMANDS(FSkeletonEditingToolActionCommands);