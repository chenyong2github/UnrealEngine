// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsManagerActions.h"
#include "EditorStyleSet.h"
#include "ModelingToolsEditorModeStyle.h"

#define LOCTEXT_NAMESPACE "ModelingToolsManagerCommands"



FModelingToolsManagerCommands::FModelingToolsManagerCommands() :
	TCommands<FModelingToolsManagerCommands>(
		"ModelingToolsManagerCommands", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsToolCommands", "Modeling Mode - Tools"), // Localized context name for displaying
		NAME_None, // Parent
		FModelingToolsEditorModeStyle::Get()->GetStyleSetName() // Icon Style Set
		)
{
}


void FModelingToolsManagerCommands::RegisterCommands()
{
	UI_COMMAND(BeginAddPrimitiveTool, "Primitive", "Start the Add Primitive Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginAddPatchTool, "Patch", "Start the Add Patch Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginDrawPolygonTool, "Polygon", "Start the Draw Polygon Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginDrawPolyPathTool, "PolyPath", "Start the Draw PolyPath Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginShapeSprayTool, "ShapeSpray", "Start the Shape Spray Tool", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BeginSculptMeshTool, "Sculpt", "Start the Sculpt Mesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginPolyEditTool, "PolyEdit", "Start the PolyEdit Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginTriEditTool, "TriEdit", "Start the Triangle Edit Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginPolyDeformTool, "PolyDeform", "Start the PolyDeform Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginSmoothMeshTool, "Smooth", "Start the Smooth Mesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginDisplaceMeshTool, "Displace", "Start the Displace Mesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginMeshSpaceDeformerTool, "SpaceDeform", "Start the Mesh Space Deformer Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginTransformMeshesTool, "Transform", "Start the Transform Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginEditPivotTool, "Edit Pivot", "Start the Edit Pivot Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginBakeTransformTool, "Bake XForm", "Start the Bake Transform Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginCombineMeshesTool, "Combine", "Combine the selected Mesh Assets into a new Asset", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginDuplicateMeshesTool, "Duplicate", "Duplicate the selected Mesh Asset into a new Asset", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginAlignObjectsTool, "Align", "Start the Align Objects Tool", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BeginRemeshSculptMeshTool, "DynaSculpt", "Start the Liquid Sculpt Mesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginRemeshMeshTool, "Remesh", "Start the Remesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginSimplifyMeshTool, "Simplify", "Start the Simplify Mesh Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginEditNormalsTool, "Normals", "Start the Edit Normals Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginRemoveOccludedTrianglesTool, "Jacketing", "Start the Jacketing Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginUVProjectionTool, "UVProjection", "Start the UV Projection Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginUVLayoutTool, "UVLayout", "Start the UV Layout Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginVoxelMergeTool, "VoxMerge", "Start the Voxel Merge Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginVoxelBooleanTool, "VoxBoolean", "Start the Voxel Boolean Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginPlaneCutTool, "PlaneCut", "Start the Plane Cut Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginPolygonCutTool, "PolyCut", "Start the Polygon Cut Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginGlobalUVGenerateTool, "AutoUnwrap", "Start the Global UV Unwrap Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginGroupUVGenerateTool, "GroupUnwrap", "Stat the PolyGroup UV Unwrap Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginMeshSelectionTool, "Select", "Start the Mesh Selection Tool", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BeginMeshInspectorTool, "Inspector", "Start the Mesh Inspector Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginWeldEdgesTool, "Weld Edges", "Start the Weld Edges Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginPolyGroupsTool, "PolyGroups", "Start the PolyGroups Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginEditMeshMaterialsTool, "Edit Mats", "Start the Material Editing Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginTransformUVIslandsTool, "Transform UVs", "Start the UV Island Transformation Tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginAttributeEditorTool, "Edit Attribs", "Start the Attribute Editor Tool", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(AcceptActiveTool, "Accept", "Accept the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CancelActiveTool, "Cancel", "Cancel the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CompleteActiveTool, "Complete", "Complete the active tool", EUserInterfaceActionType::Button, FInputChord());
}




#undef LOCTEXT_NAMESPACE
