// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsManagerActions.h"
#include "EditorStyleSet.h"
#include "InputCoreTypes.h"
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
	UI_COMMAND(BeginAddBoxPrimitiveTool, "Box", "Add Boxes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddCylinderPrimitiveTool, "Cylinder", "Add Cylinders", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddConePrimitiveTool, "Cone", "Add Cones", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddArrowPrimitiveTool, "Arrow", "Add Arrows", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddRectanglePrimitiveTool, "Rectangle", "Add Rectangles", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddRoundedRectanglePrimitiveTool, "Rounded Rectangle", "Add Rounded Rectangles", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddDiscPrimitiveTool, "Disc", "Add Discs", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddPuncturedDiscPrimitiveTool, "Punctured Disc", "Add PuncturedDiscs", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddTorusPrimitiveTool, "Torus", "Add Torii", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddSphericalBoxPrimitiveTool, "Spherical Box", "Add Box Parametrized Spheres", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddSpherePrimitiveTool, "Sphere", "Add Lat/Long Parametrized Spheres", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginAddPatchTool, "Patch", "Start the Add Patch Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRevolveBoundaryTool, "CutRevolve", "Start the CutRevolve Tool, which revolves a section curve created by cutting the selected Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawPolygonTool, "Polygon", "Start the Draw Polygon Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawPolyPathTool, "PolyPath", "Start the Draw PolyPath Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawAndRevolveTool, "PolyRevolve", "Start the Draw-and-Revolve Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginShapeSprayTool, "ShapeSpray", "Start the Shape Spray Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginSculptMeshTool, "Sculpt", "Start the Sculpt Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyEditTool, "PolyEdit", "Start the PolyEdit Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroupEdgeInsertionTool, "EdgeInsert", "Start the Group Edge Insertion Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEdgeLoopInsertionTool, "LoopInsert", "Start the Edge Loop Insertion Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTriEditTool, "TriEdit", "Start the Triangle Edit Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyDeformTool, "PolyDeform", "Start the PolyDeform Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSmoothMeshTool, "Smooth", "Start the Smooth Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginOffsetMeshTool, "Offset", "Start the Offset Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDisplaceMeshTool, "Displace", "Start the Displace Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshSpaceDeformerTool, "SpaceDeform", "Start the Mesh Space Deformer Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTransformMeshesTool, "Transform", "Start the Transform Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditPivotTool, "Edit Pivot", "Start the Edit Pivot Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBakeTransformTool, "Bake XForm", "Start the Bake Transform Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginCombineMeshesTool, "Combine", "Combine the selected Mesh Assets into a new Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDuplicateMeshesTool, "Duplicate", "Duplicate the selected Mesh Asset into a new Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAlignObjectsTool, "Align", "Start the Align Objects Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginRemeshSculptMeshTool, "DynaSculpt", "Start the Liquid Sculpt Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRemeshMeshTool, "Remesh", "Start the Remesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginProjectToTargetTool, "MapToMesh", "Start the Remesh To Target Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSimplifyMeshTool, "Simplify", "Start the Simplify Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditNormalsTool, "Normals", "Start the Edit Normals Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditTangentsTool, "Tangents", "Start the Edit Tangents Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRemoveOccludedTrianglesTool, "Jacketing", "Start the Jacketing Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVProjectionTool, "UVProjection", "Start the UV Projection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVLayoutTool, "UVLayout", "Start the UV Layout Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelMergeTool, "VoxMerge", "Start the Voxel Merge Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelBooleanTool, "VoxBoolean", "Start the Voxel Boolean Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelSolidifyTool, "VoxWrap", "Start the Voxel Shell Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelBlendTool, "VoxBlend", "Start the Voxel Blend Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelMorphologyTool, "VoxMorphology", "Start the Voxel Morphology Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSelfUnionTool, "SelfUnion", "Start the Mesh-Based Self-Union Tool, which will resolve self-intersections", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshBooleanTool, "Mesh Boolean", "Start the Mesh-Based Boolean Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshTrimTool, "Mesh Trim", "Start the Mesh-Based Trim Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBspConversionTool, "BSP Conversion", "Start the BSP-to-StaticMesh Conversion Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshToVolumeTool, "Mesh2Vol", "Start the Mesh to Volume Conversion Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVolumeToMeshTool, "Vol2Mesh", "Start the Volume to Mesh Conversion Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPlaneCutTool, "PlaneCut", "Start the Plane Cut Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMirrorTool, "Mirror", "Start the Mirror Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginHoleFillTool, "HoleFill", "Start the Hole Fill Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginLatticeDeformerTool, "LatticeDeformer", "Start the Lattice Deformer Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolygonCutTool, "PolyCut", "Start the Polygon Cut Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGlobalUVGenerateTool, "AutoUnwrap", "Start the Global UV Unwrap Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroupUVGenerateTool, "GroupUnwrap", "Stat the PolyGroup UV Unwrap Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshSelectionTool, "Select", "Start the Mesh Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginPhysicsInspectorTool, "PhysInspect", "Inspect Physics Geometry for selected Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSetCollisionGeometryTool, "MeshToCollision", "Convert selected Meshes to Collision Geometry", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditCollisionGeometryTool, "EditPhys", "Edit Collision Geometry for selected Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginExtractCollisionGeometryTool, "CollisionToMesh", "Convert Collision Geometry to a Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginMeshInspectorTool, "Inspector", "Start the Mesh Inspector Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginWeldEdgesTool, "Weld Edges", "Start the Weld Edges Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyGroupsTool, "PolyGroups", "Start the PolyGroups Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditMeshMaterialsTool, "Edit Mats", "Start the Material Editing Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTransformUVIslandsTool, "Transform UVs", "Start the UV Island Transformation Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshAttributePaintTool, "Paint Maps", "Start the Paint Maps Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAttributeEditorTool, "Edit Attribs", "Start the Attribute Editor Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBakeMeshAttributeMapsTool, "Bake Maps", "Start the Map Baking Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVSeamEditTool, "UVSeamEdit", "Start the UV Seam Editing Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginGroomToMeshTool, "HairHelmet", "Start the Hair Helmet Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGenerateLODMeshesTool, "GenLODs", "Start the Generate LOD Meshes Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomCardsToMeshTool, "CardsToMesh", "Start the Hair Cards to Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomMeshToCardsTool, "MeshToCards", "Start the Mesh to Hair Cards Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomCardsEditorTool, "CardsEditor", "Start the Cards Editor Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSubdividePolyTool, "Subdivide", "Start the SubdividePoly Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginGenerateStaticMeshLODAssetTool, "AutoLOD", "Start the Generate Static Mesh LOD Asset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshGroupPaintTool, "GroupPaint", "Start the Mesh PolyGroup Paint Tool", EUserInterfaceActionType::ToggleButton, FInputChord());


	UI_COMMAND(BeginPolyModelTool_FaceSelect, "Faces", "Start the PolyGroup Face Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_EdgeSelect, "Edges", "Start the PolyGroup Edge Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_VertexSelect, "Verts", "Start the PolyGroup Vertex Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_AllSelect, "Any", "Start the PolyGroup Face/Edge/Vertex Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_LoopSelect, "Loops", "Start the PolyGroup Loop Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_RingSelect, "Rings", "Start the PolyGroup Ring Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginPolyModelTool_Extrude, "Extrude", "Start the PolyGroup Extrude Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Offset, "Offset", "Start the PolyGroup Offset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Inset, "Inset", "Start the PolyGroup Inset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Outset, "Outset", "Start the PolyGroup Outset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_CutFaces, "Cut", "Start the PolyGroup Cut Faces Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(AcceptActiveTool, "Accept", "Accept the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CancelActiveTool, "Cancel", "Cancel the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CompleteActiveTool, "Complete", "Complete the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CancelOrCompleteActiveTool, "Cancel or Complete", "Cancel or complete the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));
}




#undef LOCTEXT_NAMESPACE
