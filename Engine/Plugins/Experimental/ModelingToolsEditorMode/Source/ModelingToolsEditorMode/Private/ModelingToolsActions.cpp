// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsActions.h"
#include "EditorStyleSet.h"
#include "DynamicMeshSculptTool.h"
#include "MeshInspectorTool.h"
#include "DrawPolygonTool.h"
#include "PolygonMeshDeformTool.h"
#include "ShapeSprayTool.h"
#include "MeshSpaceDeformerTool.h"
#include "MeshSelectionTool.h"
#include "TransformMeshesTool.h"


#define LOCTEXT_NAMESPACE "ModelingToolsCommands"



FModelingToolActionCommands::FModelingToolActionCommands() : 
	TInteractiveToolCommands<FModelingToolActionCommands>(
		"ModelingToolsEditMode", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsEditMode", "Modeling Tools Mode"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
	)
{
}


void FModelingToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UDynamicMeshSculptTool>());
	ToolCDOs.Add(GetMutableDefault<UMeshInspectorTool>());
	ToolCDOs.Add(GetMutableDefault<UDrawPolygonTool>());
	ToolCDOs.Add(GetMutableDefault<UPolygonMeshDeformTool>());
	ToolCDOs.Add(GetMutableDefault<UMeshSpaceDeformerTool>());
	ToolCDOs.Add(GetMutableDefault<UShapeSprayTool>());
	ToolCDOs.Add(GetMutableDefault<UMeshSelectionTool>());
	ToolCDOs.Add(GetMutableDefault<UTransformMeshesTool>());
}



#undef LOCTEXT_NAMESPACE

