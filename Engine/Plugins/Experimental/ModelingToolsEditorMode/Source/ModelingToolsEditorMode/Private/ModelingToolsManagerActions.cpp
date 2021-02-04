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
	UI_COMMAND(BeginAddBoxPrimitiveTool, "Box", "Create new Box StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddCylinderPrimitiveTool, "Cyl", "Create new Cylinder StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddConePrimitiveTool, "Cone", "Create new Cone StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddArrowPrimitiveTool, "Arrow", "Create new Arrow StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddRectanglePrimitiveTool, "Rect", "Create new Rectangle StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddRoundedRectanglePrimitiveTool, "RndRct", "Create new Rounded Rectangle StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddDiscPrimitiveTool, "Disc", "Create new Disc StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddPuncturedDiscPrimitiveTool, "Circle", "Create new Punctured Disc StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddTorusPrimitiveTool, "Torus", "Create new Torus StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddSphericalBoxPrimitiveTool, "SphrB", "Create new Sphere StaticMesh Assets with Box Parameterization", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAddSpherePrimitiveTool, "SphrA", "Create new Sphere StaticMesh Assets with Lat/Long Parameterization", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginAddPatchTool, "Patch", "Add Patch", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRevolveBoundaryTool, "CutRev", "Cut Mesh and Revolve Cut Section Curve into New Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawPolygonTool, "PolyExt", "Draw/Extrude Polygons to create new StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawPolyPathTool, "PathExt", "Draw/Extrude PolyPaths to create new StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDrawAndRevolveTool, "PathRev", "Draw and Revolve a PolyPath to create a new StaticMesh Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginShapeSprayTool, "Spray", "Shape Spray", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginSculptMeshTool, "VSclpt", "Vertex Sculpting", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyEditTool, "PolyEd", "Edit Mesh via PolyGroups", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroupEdgeInsertionTool, "EdgeIns", "Insert PolyGroup Edge", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEdgeLoopInsertionTool, "LoopIns", "Insert PolyGroup Edge Loop", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTriEditTool, "TriEd", "Edit Mesh via Triangles", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyDeformTool, "PolyDef", "Deform Mesh via PolyGroups", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSmoothMeshTool, "Smooth", "Smooth Mesh surface", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginOffsetMeshTool, "Offset", "Offset Mesh surface", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDisplaceMeshTool, "Displce", "Displace Mesh surface with optional subdivision", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshSpaceDeformerTool, "Warp", "Reshape Mesh using Space Deformers", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTransformMeshesTool, "XForm", "Transform selected Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditPivotTool, "Pivot", "Edit Mesh Pivots", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBakeTransformTool, "BakeRS", "Bake Scale/Rotation into Mesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginCombineMeshesTool, "Append", "Combine Selection into new StaticMesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginDuplicateMeshesTool, "Dupe", "Duplicate Selection into new StaticMesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAlignObjectsTool, "Align", "Align Objects", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginRemeshSculptMeshTool, "DSclpt", "Dynamic Mesh Sculpting", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRemeshMeshTool, "Remesh", "Retriangulate Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginProjectToTargetTool, "Project", "Map/Remesh onto Target Mesh (second selection)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSimplifyMeshTool, "Simplfy", "Simplify Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditNormalsTool, "Nrmls", "Modify/Update Mesh Normals", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditTangentsTool, "Tngnts", "Update Mesh Tangents", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRemoveOccludedTrianglesTool, "Jacket", "Remove Hidden Triangles from selected Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVProjectionTool, "Project", "Set UVs from Projection", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVLayoutTool, "Layout", "Re-Pack Existing UVs", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelMergeTool, "VoxMrg", "Merge Selected Meshes (Voxel-Based)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelBooleanTool, "VoxBool", "Boolean Selected Meshes (Voxel-Based)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelSolidifyTool, "VoxWrap", "Wrap Selected Meshes (Voxel-Based)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelBlendTool, "VoxBlnd", "Blend Selected Meshes (Voxel-Based)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVoxelMorphologyTool, "VoxMrph", "Offset/Inset Selected Meshes (Voxel-Based)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSelfUnionTool, "Merge", "Self-Union Selected Meshes to resolve Self-Intersections", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshBooleanTool, "Boolean", "Apply Boolean/CSG operation to selected Mesh pair", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshTrimTool, "Trim", "Trim/Cut selected mesh with second mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBspConversionTool, "BSPConv", "Convert BSP to StaticMesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshToVolumeTool, "Msh2Vol", "Convert Mesh to Volume", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginVolumeToMeshTool, "Vol2Msh", "Convert Volume to new Mesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPlaneCutTool, "PlnCut", "Cut Selected Meshes with Plane", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMirrorTool, "Mirror", "Mirror Selected Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginHoleFillTool, "HFill", "Fill Holes in Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginLatticeDeformerTool, "Grid", "Deform Mesh with 3D Lattice/Grid", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolygonCutTool, "PolyCut", "Cut Mesh with Extruded Polygon", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGlobalUVGenerateTool, "AutoUV", "Auto-Unwrap and Pack UVs for Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroupUVGenerateTool, "Unwrap", "Recalculate UV unwrapping for Mesh regions", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshSelectionTool, "TriSel", "Select and Edit Mesh Triangles", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginPhysicsInspectorTool, "PInspct", "Inspect Physics Geometry for selected Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSetCollisionGeometryTool, "Msh2Coll", "Convert Selected Meshes to Simple Collision Geometry (for last Selected)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditCollisionGeometryTool, "EditPhys", "Edit Simple Collision Geometry for selected Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginExtractCollisionGeometryTool, "Coll2Msh", "Convert Simple Collision Geometry to new Mesh Asset", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginMeshInspectorTool, "Inspct", "Inspect Mesh Attributes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginWeldEdgesTool, "Weld", "Weld Overlapping Mesh Edges", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyGroupsTool, "GenGrps", "Generate new PolyGroups", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginEditMeshMaterialsTool, "MatEd", "Assign Materials to Selected Triangles", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTransformUVIslandsTool, "XForm", "Transform UV Islands in UV Space", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshAttributePaintTool, "MapPnt", "Paint Attribute Maps", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginAttributeEditorTool, "AttrEd", "Edit/Configure Mesh Attributes", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginBakeMeshAttributeMapsTool, "BakeTx", "Bake Image Maps for Target Mesh (optionally from second Source Mesh)", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginUVSeamEditTool, "SeamEd", "Add UV Seams to Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginGroomToMeshTool, "Helmet", "Generate Helmet Mesh for Selected Groom", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGenerateLODMeshesTool, "HlmLOD", "Generate LODS for Hair Helmet", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomCardsToMeshTool, "CardsToMesh", "Hair Cards to Mesh Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomMeshToCardsTool, "MeshToCards", "Mesh to Hair Cards Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginGroomCardsEditorTool, "CardsEd", "Edit Hair Cards", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSubdividePolyTool, "SubD", "Subdivide Mesh Polygroups or Triangles", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginGenerateStaticMeshLODAssetTool, "AutoLOD", "Generate Static Mesh LOD Asset", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginMeshGroupPaintTool, "GrpPnt", "Paint New Mesh Polygroups", EUserInterfaceActionType::ToggleButton, FInputChord());


	UI_COMMAND(BeginPolyModelTool_FaceSelect, "Faces", "PolyGroup Face Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_EdgeSelect, "Edges", "PolyGroup Edge Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_VertexSelect, "Verts", "PolyGroup Vertex Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_AllSelect, "Any", "PolyGroup Face/Edge/Vertex Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_LoopSelect, "Loops", "PolyGroup Loop Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_RingSelect, "Rings", "PolyGroup Ring Selection Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(BeginPolyModelTool_Extrude, "Extrude", "PolyGroup Extrude Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Offset, "Offset", "PolyGroup Offset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Inset, "Inset", "PolyGroup Inset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_Outset, "Outset", "PolyGroup Outset Tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginPolyModelTool_CutFaces, "Cut", "PolyGroup Cut Faces Tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(AcceptActiveTool, "Accept", "Accept the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CancelActiveTool, "Cancel", "Cancel the active tool", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CompleteActiveTool, "Done", "Complete the active tool", EUserInterfaceActionType::Button, FInputChord());

	// Note that passing a chord into one of these calls hooks the key press to the respective action. 
	UI_COMMAND(AcceptOrCompleteActiveTool, "Accept or Complete", "Accept or complete the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Enter));
	UI_COMMAND(CancelOrCompleteActiveTool, "Cancel or Complete", "Cancel or complete the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));
}




#undef LOCTEXT_NAMESPACE
