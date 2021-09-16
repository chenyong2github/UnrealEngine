// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshEditorModelingMode.h"
#include "StaticMeshEditorModelingToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "ToolTargetManager.h"
#include "ModelingToolsManagerActions.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "Tools/GenerateStaticMeshLODAssetTool.h"
#include "Tools/LODManagerTool.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorModelingMode"

const FEditorModeID UStaticMeshEditorModelingMode::Id("StaticMeshEditorModelingMode");

UStaticMeshEditorModelingMode::UStaticMeshEditorModelingMode()
{
	Info = FEditorModeInfo(Id, LOCTEXT("StaticMeshEditorModelingMode", "StaticMeshEditorModelingMode"), FSlateIcon(), true);
}

void UStaticMeshEditorModelingMode::Enter()
{
	UEdMode::Enter();

	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(GetToolManager()));

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();

	RegisterTool(ToolManagerCommands.BeginGenerateStaticMeshLODAssetTool, TEXT("BeginGenerateStaticMeshLODAssetTool"), NewObject<UGenerateStaticMeshLODAssetToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginLODManagerTool, TEXT("BeginLODManagerTool"), NewObject<ULODManagerToolBuilder>());
}

bool UStaticMeshEditorModelingMode::UsesToolkits() const
{ 
	return true; 
}

void UStaticMeshEditorModelingMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FStaticMeshEditorModelingToolkit);
}

#undef LOCTEXT_NAMESPACE
