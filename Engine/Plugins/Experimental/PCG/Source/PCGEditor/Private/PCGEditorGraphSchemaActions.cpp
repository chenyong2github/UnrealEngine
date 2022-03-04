// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphSchemaActions.h"

#include "PCGGraph.h"
#include "PCGEditorGraph.h"
#include "PCGEditorGraphNode.h"
#include "PCGSettings.h"

#include "EdGraph/EdGraph.h"

UEdGraphNode* FPCGEditorGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	UPCGEditorGraph* EditorGraph = Cast<UPCGEditorGraph>(ParentGraph);
	if (!EditorGraph)
	{
		return nullptr;
	}

	UPCGGraph* PCGGraph = EditorGraph->GetPCGGraph();
	if (!PCGGraph)
	{
		return nullptr;
	}

	UPCGSettings* DefaultNodeSettings = nullptr;
	UPCGNode* NewPCGNode = PCGGraph->AddNodeOfType(SettingsClass, DefaultNodeSettings);

	FGraphNodeCreator<UPCGEditorGraphNode> NodeCreator(*EditorGraph);
	UPCGEditorGraphNode* NewNode = NodeCreator.CreateNode(bSelectNewNode);
	NewNode->Construct(NewPCGNode, EPCGEditorGraphNodeType::Settings);
	NewNode->NodePosX = Location.X;
	NewNode->NodePosY = Location.Y;
	NodeCreator.Finalize();

	return NewNode;
}
