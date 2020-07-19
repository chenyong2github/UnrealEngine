// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNodeGraphNotify.h"

#include "EdGraph/EdGraph.h"

#include "OptimusEditorGraph.generated.h"

struct FSlateBrush;
class UOptimusNode;
class UOptimusDeformer;
class UOptimusEditorGraphNode;

UCLASS()
class UOptimusEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UOptimusEditorGraph();

	void InitFromNodeGraph(UOptimusNodeGraph* InNodeGraph);

	UOptimusNodeGraph* GetModelGraph() const { return NodeGraph; }

	UOptimusEditorGraphNode* FindGraphNodeFromModelNode(UOptimusNode* Node);

	///
	const FSlateBrush* GetGraphTypeIcon() const;

private:
	void HandleNodeGraphModified(
		EOptimusNodeGraphNotifyType InNotifyType, 
		UOptimusNodeGraph *InNodeGraph, 
		UObject *InSubject
		);

	UOptimusEditorGraphNode* AddGraphNodeFromModelNode(UOptimusNode* InModelNode);

	UOptimusNodeGraph* NodeGraph;
};
