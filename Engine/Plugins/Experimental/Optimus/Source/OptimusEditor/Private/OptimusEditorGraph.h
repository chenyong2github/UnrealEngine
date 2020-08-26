// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNodeGraphNotify.h"

#include "EdGraph/EdGraph.h"
#include "Containers/Set.h"

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

	void Reset();

	UOptimusNodeGraph* GetModelGraph() const { return NodeGraph; }

	UOptimusEditorGraphNode* FindGraphNodeFromModelNode(const UOptimusNode* Node);

	const TSet<UOptimusEditorGraphNode*> &GetSelectedNodes() const { return SelectedNodes; }

	///
	const FSlateBrush* GetGraphTypeIcon() const;

protected:
	friend class FOptimusEditor;

	void SetSelectedNodes(const TSet<UOptimusEditorGraphNode*>& InSelectedNodes)
	{
		SelectedNodes = InSelectedNodes;
	}

private:
	void HandleThisGraphModified(
		const FEdGraphEditAction &InEditAction
	);

	void HandleNodeGraphModified(
		EOptimusNodeGraphNotifyType InNotifyType, 
		UOptimusNodeGraph *InNodeGraph, 
		UObject *InSubject
		);

	UOptimusEditorGraphNode* AddGraphNodeFromModelNode(UOptimusNode* InModelNode);

	UOptimusNodeGraph* NodeGraph;

	TSet<UOptimusEditorGraphNode*> SelectedNodes;
};
