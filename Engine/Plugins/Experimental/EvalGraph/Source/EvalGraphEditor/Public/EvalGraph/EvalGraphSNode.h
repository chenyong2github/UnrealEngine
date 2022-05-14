// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"
#include "SGraphNode.h"

#include "EvalGraphSNode.generated.h"

class UEvalGraphEdNode;

//
// SEvalGraphEdNode
//

class EVALGRAPHEDITOR_API SEvalGraphEdNode : public SGraphNode
{
	typedef SGraphNode Super;

public:
	SLATE_BEGIN_ARGS(SEvalGraphEdNode)
		: _GraphNodeObj(nullptr)
	{}

	SLATE_ARGUMENT(UEvalGraphEdNode*, GraphNodeObj)

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, UEvalGraphEdNode* InNode);

	// SGraphNode interface
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

};

//
// Action to add a node to the graph
//
USTRUCT()
struct EVALGRAPHEDITOR_API FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

public:
	FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode() : NodeTemplate(nullptr) {}

	FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode(const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InGrouping), NodeTemplate(nullptr) {}

	static TSharedPtr<FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode> CreateAction(UEdGraph* Owner, const FName & NodeTypeName);

	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	UEvalGraphEdNode* NodeTemplate;
};
