// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphSNode.h"

#include "EvalGraph/EvalGraphEdNode.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "EvalGraph/EvalGraphObject.h"
#include "EvalGraph/EvalGraph.h"
#include "Logging/LogMacros.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SEvalGraphEdNode"
//
// SEvalGraphEdNode
//

void SEvalGraphEdNode::Construct(const FArguments& InArgs, UEvalGraphEdNode* InNode)
{
	GraphNode = InNode;
	UEvalGraph* Graph = Cast<UEvalGraph>(InNode->GetOuter());
	if( ensure(Graph) )
	{
		if (Eg::FNodeFactory* Factory = Eg::FNodeFactory::GetInstance())
		{
			if (TSharedPtr<Eg::FNode> EgNode = Factory->NewNodeFromRegisteredType(*Graph->GetEvalGraph(), { FGuid::NewGuid(),FName(InNode->GetName()),FName(InNode->GetName()) }))
			{
				InNode->SetEgGraph(Graph->GetEvalGraph());
				InNode->SetEgNode(EgNode->GetGuid());
				InNode->AllocateDefaultPins();
			}
		}
	}
	UpdateGraphNode();
}

FReply SEvalGraphEdNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return Super::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}



//
// FAssetSchemaAction_EvalGraph_CreateNode_Example
//
TSharedPtr<FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode> FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::CreateAction(UEdGraph* Owner, const FName & InNodeTypeName)
{
	const FText AddToolTip = LOCTEXT("EvalGraphNodeTooltip_Example", "Add a Dataflow node.");
	const FText NodeName = FText::FromString(*InNodeTypeName.ToString());
	const FText Catagory = LOCTEXT("EvalGraphNodeDescription_Example", "Dataflow"); 	
	TSharedPtr<FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode> NewNodeAction(
		new FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode(Catagory, NodeName, AddToolTip, 0));
	NewNodeAction->NodeTemplate = NewObject<UEvalGraphEdNode>(Owner, UEvalGraphEdNode::StaticClass(), InNodeTypeName);
	//NewNodeAction->NodeTemplate->GenericGraphNode = NewObject<UGenericGraphNode>(NewNodeAction->NodeTemplate, Graph->NodeType);
	//NewNodeAction->NodeTemplate->GenericGraphNode->Graph = Graph;
	return NewNodeAction;
}


UEdGraphNode* FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	UEdGraphNode* ResultNode = nullptr;

	if (NodeTemplate != nullptr)
	{
		//const FScopedTransaction Transaction(LOCTEXT("EvalGraphNewNode", "Generic Graph Editor: New Node"));
		ParentGraph->Modify();
		if (FromPin != nullptr)
			FromPin->Modify();

		NodeTemplate->Rename(nullptr, ParentGraph);
		ParentGraph->AddNode(NodeTemplate, true, bSelectNewNode);

		NodeTemplate->CreateNewGuid();
		NodeTemplate->PostPlacedNewNode();
		//NodeTemplate->AllocateDefaultPins();
		NodeTemplate->AutowireNewNode(FromPin);

		NodeTemplate->NodePosX = Location.X;
		NodeTemplate->NodePosY = Location.Y;

		//NodeTemplate->GenericGraphNode->SetFlags(RF_Transactional);
		NodeTemplate->SetFlags(RF_Transactional);

		ResultNode = NodeTemplate;
	}

	return ResultNode;
}

void FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdGraphSchemaAction::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(NodeTemplate);
}

#undef LOCTEXT_NAMESPACE
