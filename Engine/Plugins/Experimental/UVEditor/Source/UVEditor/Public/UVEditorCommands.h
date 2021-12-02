// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class UVEDITOR_API FUVEditorCommands : public TCommands<FUVEditorCommands>
{
public:

	FUVEditorCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenUVEditor;
	TSharedPtr<FUICommandInfo> ApplyChanges;

	TSharedPtr<FUICommandInfo> BeginSelectTool;
	TSharedPtr<FUICommandInfo> BeginLayoutTool;
	TSharedPtr<FUICommandInfo> BeginParameterizeMeshTool;
	TSharedPtr<FUICommandInfo> BeginChannelEditTool;
	TSharedPtr<FUICommandInfo> BeginSeamTool;
	TSharedPtr<FUICommandInfo> BeginRecomputeUVsTool;

	TSharedPtr<FUICommandInfo> SewAction;
	TSharedPtr<FUICommandInfo> SplitAction;
	TSharedPtr<FUICommandInfo> IslandConformalUnwrapAction;

	TSharedPtr<FUICommandInfo> AcceptOrCompleteActiveTool;
	TSharedPtr<FUICommandInfo> CancelOrCompleteActiveTool;

	TSharedPtr<FUICommandInfo> VertexSelection;
	TSharedPtr<FUICommandInfo> EdgeSelection;
	TSharedPtr<FUICommandInfo> TriangleSelection;
	TSharedPtr<FUICommandInfo> IslandSelection;
	TSharedPtr<FUICommandInfo> FullMeshSelection;

	TSharedPtr<FUICommandInfo> EnableOrbitCamera;
	TSharedPtr<FUICommandInfo> EnableFlyCamera;
	TSharedPtr<FUICommandInfo> ToggleBackground;
};