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
	UpdateGraphNode();
}

FReply SEvalGraphEdNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return Super::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}



//
// Add a menu option to create a graph node.
//
TSharedPtr<FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode> FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::CreateAction(UEdGraph* ParentGraph, const FName & InNodeTypeName)
{
 	const FText AddToolTip = LOCTEXT("EvalGraphNodeTooltip_Example", "Add a Dataflow node.");
	const FText NodeName = FText::FromString(*InNodeTypeName.ToString());
	const FText Catagory = LOCTEXT("EvalGraphNodeDescription_Example", "Dataflow"); 	
	TSharedPtr<FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode> NewNodeAction(
		new FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode(InNodeTypeName, Catagory, NodeName, AddToolTip, 0));
	return NewNodeAction;
}

//
//  Created the EdGraph node and bind the guids to the EvalGraph's node. 
//
UEdGraphNode* FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (UEvalGraph* EvalGraph = Cast<UEvalGraph>(ParentGraph))
	{
		const FName NodeName(GetMenuDescription().ToString());
		if (UEvalGraphEdNode* EdNode = NewObject<UEvalGraphEdNode>(EvalGraph, UEvalGraphEdNode::StaticClass(), NodeName))
		{
			//const FScopedTransaction Transaction(LOCTEXT("EvalGraphNewNode", "Generic Graph Editor: New Node"));
			EvalGraph->Modify();
			if (FromPin != nullptr)
				FromPin->Modify();

			EvalGraph->AddNode(EdNode, true, bSelectNewNode);

			EdNode->CreateNewGuid();
			EdNode->PostPlacedNewNode();

			if (Eg::FNodeFactory* Factory = Eg::FNodeFactory::GetInstance())
			{
				if (TSharedPtr<Eg::FNode> EgNode = Factory->NewNodeFromRegisteredType(*EvalGraph->GetEvalGraph(), { FGuid::NewGuid(),NodeTypeName,NodeName}))
				{
					EdNode->SetEgGraph(EvalGraph->GetEvalGraph());
					EdNode->SetEgNodeGuid(EgNode->GetGuid());
					EdNode->AllocateDefaultPins();
				}
			}

			//EdNode->AllocateDefaultPins();
			EdNode->AutowireNewNode(FromPin);

			EdNode->NodePosX = Location.X;
			EdNode->NodePosY = Location.Y;

			//EdNode->GenericGraphNode->SetFlags(RF_Transactional);
			EdNode->SetFlags(RF_Transactional);

			return EdNode;
		}
	}
	return nullptr;
}

//void FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::AddReferencedObjects(FReferenceCollector& Collector)
//{
//	FEdGraphSchemaAction::AddReferencedObjects(Collector);
//	Collector.AddReferencedObject(NodeTemplate);
//}

#undef LOCTEXT_NAMESPACE
