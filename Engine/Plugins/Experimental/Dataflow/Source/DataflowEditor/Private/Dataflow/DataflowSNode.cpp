// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSNode.h"

#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowCore.h"
#include "Logging/LogMacros.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SDataflowEdNode"
//
// SDataflowEdNode
//

void SDataflowEdNode::Construct(const FArguments& InArgs, UDataflowEdNode* InNode)
{
	GraphNode = InNode;
	UpdateGraphNode();
}

FReply SDataflowEdNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return Super::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}



//
// Add a menu option to create a graph node.
//
TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(UEdGraph* ParentGraph, const FName & InNodeTypeName)
{
 	const FText AddToolTip = LOCTEXT("DataflowNodeTooltip_Example", "Add a Dataflow node.");
	const FText NodeName = FText::FromString(*InNodeTypeName.ToString());
	const FText Catagory = LOCTEXT("DataflowNodeDescription_Example", "Dataflow"); 	
	TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NewNodeAction(
		new FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode(InNodeTypeName, Catagory, NodeName, AddToolTip, 0));
	return NewNodeAction;
}

//
//  Created the EdGraph node and bind the guids to the Dataflow's node. 
//
UEdGraphNode* FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (UDataflow* Dataflow = Cast<UDataflow>(ParentGraph))
	{
		const FName NodeName = MakeUniqueObjectName(Dataflow, UDataflowEdNode::StaticClass(), FName(GetMenuDescription().ToString()));
		if (UDataflowEdNode* EdNode = NewObject<UDataflowEdNode>(Dataflow, UDataflowEdNode::StaticClass(), NodeName))
		{
			//const FScopedTransaction Transaction(LOCTEXT("DataflowNewNode", "Generic Graph Editor: New Node"));
			Dataflow->Modify();
			if (FromPin != nullptr)
				FromPin->Modify();

			Dataflow->AddNode(EdNode, true, bSelectNewNode);

			EdNode->CreateNewGuid();
			EdNode->PostPlacedNewNode();

			if (Dataflow::FNodeFactory* Factory = Dataflow::FNodeFactory::GetInstance())
			{
				if (TSharedPtr<Dataflow::FNode> DataflowNode = Factory->NewNodeFromRegisteredType(*Dataflow->GetDataflow(), { FGuid::NewGuid(),NodeTypeName,NodeName}))
				{
					EdNode->SetDataflowGraph(Dataflow->GetDataflow());
					EdNode->SetDataflowNodeGuid(DataflowNode->GetGuid());
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

//void FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::AddReferencedObjects(FReferenceCollector& Collector)
//{
//	FEdGraphSchemaAction::AddReferencedObjects(Collector);
//	Collector.AddReferencedObject(NodeTemplate);
//}

#undef LOCTEXT_NAMESPACE
