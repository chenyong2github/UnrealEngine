// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"


/**
 * TInteractiveToolCommands implementation for this module that provides standard Editor hotkey support
 */
class FModelingToolsManagerCommands : public TCommands<FModelingToolsManagerCommands>
{
public:
	FModelingToolsManagerCommands();


	TSharedPtr<FUICommandInfo> BeginAddPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginDrawPolygonTool;
	TSharedPtr<FUICommandInfo> BeginShapeSprayTool;

	TSharedPtr<FUICommandInfo> BeginSculptMeshTool;
	TSharedPtr<FUICommandInfo> BeginPolyEditTool;
	TSharedPtr<FUICommandInfo> BeginSmoothMeshTool;
	TSharedPtr<FUICommandInfo> BeginDisplaceMeshTool;
	TSharedPtr<FUICommandInfo> BeginMeshSpaceDeformerTool;
	TSharedPtr<FUICommandInfo> BeginTransformMeshesTool;

	TSharedPtr<FUICommandInfo> BeginRemeshSculptMeshTool;
	TSharedPtr<FUICommandInfo> BeginRemeshMeshTool;
	TSharedPtr<FUICommandInfo> BeginSimplifyMeshTool;
	TSharedPtr<FUICommandInfo> BeginEditNormalsTool;
	TSharedPtr<FUICommandInfo> BeginUVProjectionTool;
	TSharedPtr<FUICommandInfo> BeginPlaneCutTool;
	TSharedPtr<FUICommandInfo> BeginPolygonOnMeshTool;
	TSharedPtr<FUICommandInfo> BeginVoxelMergeTool;
	TSharedPtr<FUICommandInfo> BeginVoxelBooleanTool;
	TSharedPtr<FUICommandInfo> BeginMeshSelectionTool;

	TSharedPtr<FUICommandInfo> BeginMeshInspectorTool;
	TSharedPtr<FUICommandInfo> BeginParameterizeMeshTool;
	TSharedPtr<FUICommandInfo> BeginWeldEdgesTool;
	TSharedPtr<FUICommandInfo> BeginPolyGroupsTool;
	TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;

	TSharedPtr<FUICommandInfo> AcceptActiveTool;
	TSharedPtr<FUICommandInfo> CancelActiveTool;
	TSharedPtr<FUICommandInfo> CompleteActiveTool;


	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};