// Copyright Epic Games, Inc. All Rights Reserved.

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
	TSharedPtr<FUICommandInfo> BeginAddPatchTool;
	TSharedPtr<FUICommandInfo> BeginDrawPolygonTool;
	TSharedPtr<FUICommandInfo> BeginDrawPolyPathTool;
	TSharedPtr<FUICommandInfo> BeginShapeSprayTool;

	TSharedPtr<FUICommandInfo> BeginSculptMeshTool;
	TSharedPtr<FUICommandInfo> BeginPolyEditTool;
	TSharedPtr<FUICommandInfo> BeginTriEditTool;
	TSharedPtr<FUICommandInfo> BeginPolyDeformTool;
	TSharedPtr<FUICommandInfo> BeginSmoothMeshTool;
	TSharedPtr<FUICommandInfo> BeginDisplaceMeshTool;
	TSharedPtr<FUICommandInfo> BeginMeshSpaceDeformerTool;
	TSharedPtr<FUICommandInfo> BeginTransformMeshesTool;
	TSharedPtr<FUICommandInfo> BeginEditPivotTool;
	TSharedPtr<FUICommandInfo> BeginBakeTransformTool;
	TSharedPtr<FUICommandInfo> BeginCombineMeshesTool;
	TSharedPtr<FUICommandInfo> BeginDuplicateMeshesTool;
	TSharedPtr<FUICommandInfo> BeginAlignObjectsTool;

	TSharedPtr<FUICommandInfo> BeginRemeshSculptMeshTool;
	TSharedPtr<FUICommandInfo> BeginRemeshMeshTool;
	TSharedPtr<FUICommandInfo> BeginSimplifyMeshTool;
	TSharedPtr<FUICommandInfo> BeginEditNormalsTool;
	TSharedPtr<FUICommandInfo> BeginRemoveOccludedTrianglesTool;
	TSharedPtr<FUICommandInfo> BeginUVProjectionTool;
	TSharedPtr<FUICommandInfo> BeginUVLayoutTool;
	TSharedPtr<FUICommandInfo> BeginPlaneCutTool;
	TSharedPtr<FUICommandInfo> BeginPolygonCutTool;
	TSharedPtr<FUICommandInfo> BeginVoxelMergeTool;
	TSharedPtr<FUICommandInfo> BeginVoxelBooleanTool;
	TSharedPtr<FUICommandInfo> BeginMeshSelectionTool;

	TSharedPtr<FUICommandInfo> BeginMeshInspectorTool;
	TSharedPtr<FUICommandInfo> BeginGlobalUVGenerateTool;
	TSharedPtr<FUICommandInfo> BeginGroupUVGenerateTool;
	TSharedPtr<FUICommandInfo> BeginWeldEdgesTool;
	TSharedPtr<FUICommandInfo> BeginPolyGroupsTool;
	TSharedPtr<FUICommandInfo> BeginEditMeshMaterialsTool;
	TSharedPtr<FUICommandInfo> BeginTransformUVIslandsTool;
	TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;

	TSharedPtr<FUICommandInfo> AcceptActiveTool;
	TSharedPtr<FUICommandInfo> CancelActiveTool;
	TSharedPtr<FUICommandInfo> CompleteActiveTool;


	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};