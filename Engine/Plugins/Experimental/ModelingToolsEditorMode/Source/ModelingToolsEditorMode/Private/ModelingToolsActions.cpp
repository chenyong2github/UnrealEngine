// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsActions.h"
#include "EditorStyleSet.h"
#include "DynamicMeshSculptTool.h"
#include "MeshInspectorTool.h"
#include "DrawPolygonTool.h"
#include "EditMeshPolygonsTool.h"
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
	ToolCDOs.Add(GetMutableDefault<UMeshInspectorTool>());
	ToolCDOs.Add(GetMutableDefault<UDrawPolygonTool>());
	ToolCDOs.Add(GetMutableDefault<UEditMeshPolygonsTool>());
	ToolCDOs.Add(GetMutableDefault<UMeshSpaceDeformerTool>());
	ToolCDOs.Add(GetMutableDefault<UShapeSprayTool>());
}



void FModelingToolActionCommands::RegisterAllToolActions()
{
	FModelingToolActionCommands::Register();
	FSculptToolActionCommands::Register();
	FDrawPolygonToolActionCommands::Register();
	FTransformToolActionCommands::Register();
	FMeshSelectionToolActionCommands::Register();
}

void FModelingToolActionCommands::UnregisterAllToolActions()
{
	FModelingToolActionCommands::Unregister();
	FSculptToolActionCommands::Unregister();
	FDrawPolygonToolActionCommands::Unregister();
	FTransformToolActionCommands::Unregister();
	FMeshSelectionToolActionCommands::Unregister();
}



void FModelingToolActionCommands::UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind)
{
#define UPDATE_BINDING(CommandsType)  if (!bUnbind) CommandsType::Get().BindCommandsForCurrentTool(UICommandList, Tool); else CommandsType::Get().UnbindActiveCommands(UICommandList);


	if (Cast<UTransformMeshesTool>(Tool) != nullptr)
	{
		UPDATE_BINDING(FTransformToolActionCommands);
	}
	else if (Cast<UDynamicMeshSculptTool>(Tool) != nullptr)
	{
		UPDATE_BINDING(FSculptToolActionCommands);
	}
	else if (Cast<UDrawPolygonTool>(Tool) != nullptr)
	{
		UPDATE_BINDING(FDrawPolygonToolActionCommands);
	}
	else if (Cast<UMeshSelectionTool>(Tool) != nullptr)
	{
		UPDATE_BINDING(FMeshSelectionToolActionCommands);
	}
	else
	{
		UPDATE_BINDING(FModelingToolActionCommands);
	}
}





FSculptToolActionCommands::FSculptToolActionCommands() :
	TInteractiveToolCommands<FSculptToolActionCommands>(
		"ModelingToolsSculptTool", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsSculptTool", "Modeling Tools - Sculpt Tool"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
		)
{
}

void FSculptToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UDynamicMeshSculptTool>());
}



FTransformToolActionCommands::FTransformToolActionCommands() :
	TInteractiveToolCommands<FTransformToolActionCommands>(
		"ModelingToolsTransformTool", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsTransformTool", "Modeling Tools - Transform Tool"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
		)
{
}

void FTransformToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UTransformMeshesTool>());
}



FDrawPolygonToolActionCommands::FDrawPolygonToolActionCommands() :
	TInteractiveToolCommands<FDrawPolygonToolActionCommands>(
		"ModelingToolsDrawPolygonTool", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsDrawPolygonTool", "Modeling Tools - Draw Polygon Tool"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
		)
{
}

void FDrawPolygonToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UDrawPolygonTool>());
}


FMeshSelectionToolActionCommands::FMeshSelectionToolActionCommands() :
	TInteractiveToolCommands<FMeshSelectionToolActionCommands>(
		"ModelingToolsMeshSelectionTool", // Context name for fast lookup
		NSLOCTEXT("Contexts", "ModelingToolsMeshSelectionTool", "Modeling Tools - Mesh Selection Tool"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
		)
{
}

void FMeshSelectionToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UMeshSelectionTool>());
}




#undef LOCTEXT_NAMESPACE

