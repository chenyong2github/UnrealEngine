// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"


/**
 * TInteractiveToolCommands implementation for this module that provides standard Editor hotkey support
 */
class MODELINGTOOLSEDITORMODE_API FModelingToolsManagerCommands : public TCommands<FModelingToolsManagerCommands>
{
public:
	FModelingToolsManagerCommands();


	TSharedPtr<FUICommandInfo> BeginAddBoxPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddCylinderPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddConePrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddArrowPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddRectanglePrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddRoundedRectanglePrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddDiscPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddPuncturedDiscPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddTorusPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddSphericalBoxPrimitiveTool;
	TSharedPtr<FUICommandInfo> BeginAddSpherePrimitiveTool;

	TSharedPtr<FUICommandInfo> BeginAddPatchTool;
	TSharedPtr<FUICommandInfo> BeginRevolveBoundaryTool;
	TSharedPtr<FUICommandInfo> BeginDrawPolygonTool;
	TSharedPtr<FUICommandInfo> BeginDrawPolyPathTool;
	TSharedPtr<FUICommandInfo> BeginDrawAndRevolveTool;
	TSharedPtr<FUICommandInfo> BeginShapeSprayTool;

	TSharedPtr<FUICommandInfo> BeginSculptMeshTool;
	TSharedPtr<FUICommandInfo> BeginPolyEditTool;
	TSharedPtr<FUICommandInfo> BeginGroupEdgeInsertionTool;
	TSharedPtr<FUICommandInfo> BeginEdgeLoopInsertionTool;
	TSharedPtr<FUICommandInfo> BeginTriEditTool;
	TSharedPtr<FUICommandInfo> BeginPolyDeformTool;
	TSharedPtr<FUICommandInfo> BeginSmoothMeshTool;
	TSharedPtr<FUICommandInfo> BeginOffsetMeshTool;
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
	TSharedPtr<FUICommandInfo> BeginProjectToTargetTool;
	TSharedPtr<FUICommandInfo> BeginSimplifyMeshTool;
	TSharedPtr<FUICommandInfo> BeginEditNormalsTool;
	TSharedPtr<FUICommandInfo> BeginEditTangentsTool;
	TSharedPtr<FUICommandInfo> BeginRemoveOccludedTrianglesTool;
	TSharedPtr<FUICommandInfo> BeginUVProjectionTool;
	TSharedPtr<FUICommandInfo> BeginUVLayoutTool;
	TSharedPtr<FUICommandInfo> BeginPlaneCutTool;
	TSharedPtr<FUICommandInfo> BeginMirrorTool;
	TSharedPtr<FUICommandInfo> BeginHoleFillTool;
	TSharedPtr<FUICommandInfo> BeginPolygonCutTool;
	TSharedPtr<FUICommandInfo> BeginVoxelMergeTool;
	TSharedPtr<FUICommandInfo> BeginVoxelBooleanTool;
	TSharedPtr<FUICommandInfo> BeginVoxelSolidifyTool;
	TSharedPtr<FUICommandInfo> BeginVoxelBlendTool;
	TSharedPtr<FUICommandInfo> BeginVoxelMorphologyTool;
	TSharedPtr<FUICommandInfo> BeginSelfUnionTool;
	TSharedPtr<FUICommandInfo> BeginMeshBooleanTool;
	TSharedPtr<FUICommandInfo> BeginMeshSelectionTool;
	TSharedPtr<FUICommandInfo> BeginBspConversionTool;
	TSharedPtr<FUICommandInfo> BeginMeshToVolumeTool;
	TSharedPtr<FUICommandInfo> BeginVolumeToMeshTool;

	TSharedPtr<FUICommandInfo> BeginPhysicsInspectorTool;
	TSharedPtr<FUICommandInfo> BeginSetCollisionGeometryTool;
	TSharedPtr<FUICommandInfo> BeginEditCollisionGeometryTool;
	TSharedPtr<FUICommandInfo> BeginExtractCollisionGeometryTool;

	TSharedPtr<FUICommandInfo> BeginMeshInspectorTool;
	TSharedPtr<FUICommandInfo> BeginGlobalUVGenerateTool;
	TSharedPtr<FUICommandInfo> BeginGroupUVGenerateTool;
	TSharedPtr<FUICommandInfo> BeginWeldEdgesTool;
	TSharedPtr<FUICommandInfo> BeginPolyGroupsTool;
	TSharedPtr<FUICommandInfo> BeginEditMeshMaterialsTool;
	TSharedPtr<FUICommandInfo> BeginTransformUVIslandsTool;
	TSharedPtr<FUICommandInfo> BeginMeshAttributePaintTool;
	TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;
	TSharedPtr<FUICommandInfo> BeginBakeMeshAttributeMapsTool;
	TSharedPtr<FUICommandInfo> BeginUVSeamEditTool;

	TSharedPtr<FUICommandInfo> BeginGroomToMeshTool;
	TSharedPtr<FUICommandInfo> BeginGenerateLODMeshesTool;
	TSharedPtr<FUICommandInfo> BeginGroomCardsToMeshTool;
	TSharedPtr<FUICommandInfo> BeginGroomMeshToCardsTool;
	TSharedPtr<FUICommandInfo> BeginGroomCardsEditorTool;

	TSharedPtr<FUICommandInfo> AcceptActiveTool;
	TSharedPtr<FUICommandInfo> CancelActiveTool;
	TSharedPtr<FUICommandInfo> CompleteActiveTool;
	TSharedPtr<FUICommandInfo> CancelOrCompleteActiveTool;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
