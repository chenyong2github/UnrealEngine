// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphSchema.h"

#include "IOptimusEditor.h"
#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"

#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusMeshAttribute.h"
#include "OptimusMeshSkinWeights.h"

#include "EdGraphSchema_K2.h"
#include "Toolkits/ToolkitManager.h"
#include "ScopedTransaction.h"


FName OptimusSchemaPinTypes::Attribute("Optimus_Attribute");
FName OptimusSchemaPinTypes::Skeleton("Optimus_Skeleton");
FName OptimusSchemaPinTypes::Mesh("Optimus_Mesh");


UOptimusEditorGraphSchema::UOptimusEditorGraphSchema()
{

}


void UOptimusEditorGraphSchema::GetGraphActions(
	FGraphActionListBuilderBase& IoActionBuilder, 
	const UEdGraphPin* InFromPin, 
	const UEdGraph* InGraph
	) const
{
	for (UClass* Class : UOptimusNode::GetAllNodeClasses())
	{
		UOptimusNode* Node = Cast< UOptimusNode>(Class->GetDefaultObject());
		if (Node == nullptr)
		{
			continue;
		}

		FText NodeName = Node->GetDisplayName();
		FText NodeCategory = FText::FromName(Node->GetNodeCategory());

		TSharedPtr< FOptimusGraphSchemaAction_NewNode> Action(
			new FOptimusGraphSchemaAction_NewNode(
				NodeCategory,
				NodeName,
				/* Tooltip */{}, 0, /* Keywords */{}
		));

		Action->NodeClass = Class;

		IoActionBuilder.AddAction(Action);
	}
}


bool UOptimusEditorGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif
	
	// Self-connections are not allowed.
	if (PinA == PinB || PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return false;
	}

	if (PinA->Direction == PinB->Direction)
	{
		return false;
	}

	if (PinA->Direction == EGPD_Input)
	{
		Swap(PinA, PinB);
	}

	UOptimusEditorGraphNode* OutputGraphNode = Cast<UOptimusEditorGraphNode>(PinA->GetOwningNode());
	UOptimusEditorGraphNode* InputGraphNode = Cast<UOptimusEditorGraphNode>(PinB->GetOwningNode());

	UOptimusNode* OutputModelNode = OutputGraphNode->ModelNode;
	UOptimusNode* InputModelNode = InputGraphNode->ModelNode;

	if (OutputModelNode->GetOuter() != InputModelNode->GetOuter())
	{
		return false;
	}

	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(OutputGraphNode->GetGraph());

	UOptimusNodePin* OutputModelPin = OutputModelNode->FindPin(PinA->GetName());
	UOptimusNodePin* InputModelPin = InputModelNode->FindPin(PinB->GetName());

	return Graph->GetModelGraph()->AddLink(OutputModelPin, InputModelPin);
}


const FPinConnectionResponse UOptimusEditorGraphSchema::CanCreateConnection(
	const UEdGraphPin* A, 
	const UEdGraphPin* B
	) const
{
	FText ResponseMessage;

	// FIXME: Add some actual validation.

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, ResponseMessage);
}


void UOptimusEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(TargetPin.GetOwningNode());
	check(GraphNode != nullptr);
	if (GraphNode != nullptr)
	{
		UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphNode->GetGraph());

		// Graph->BreakLinksToPin(&TargetPin);
	}
}


void UOptimusEditorGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{

}


void UOptimusEditorGraphSchema::GetGraphContextActions(
	FGraphContextMenuBuilder& IoContextMenuBuilder
	) const
{
	GetGraphActions(IoContextMenuBuilder, IoContextMenuBuilder.FromPin, IoContextMenuBuilder.CurrentGraph);
}


bool UOptimusEditorGraphSchema::SafeDeleteNodeFromGraph(UEdGraph* InGraph, UEdGraphNode* InNode) const
{
	UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(InNode);

	if (GraphNode)
	{
		UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphNode->GetGraph());

		return Graph->GetModelGraph()->RemoveNode(GraphNode->ModelNode);
	}

	return false;
}


FLinearColor UOptimusEditorGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == OptimusSchemaPinTypes::Mesh)
	{
		return FLinearColor::White;
	}
	else if (PinType.PinCategory == OptimusSchemaPinTypes::Attribute)
	{
		if (PinType.PinSubCategory == FName("UOptimusMeshAttribute*"))
		{
			return FLinearColor(0.4f, 0.4f, 0.8f, 1.0f);
		}
		else if (PinType.PinSubCategory == FName("UOptimusMeshSkinWeights*"))
		{
			return FLinearColor(0.4f, 0.8f, 0.8f, 1.0f);
		}
	}
	else if (PinType.PinCategory == OptimusSchemaPinTypes::Skeleton)
	{
		return FLinearColor(0.4f, 0.8f, 0.4f, 1.0f);
	}

	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}


UEdGraphNode* FOptimusGraphSchemaAction_NewNode::PerformAction(
	UEdGraph* InParentGraph, 
	UEdGraphPin* InFromPin, 
	const FVector2D InLocation, 
	bool bInSelectNewNode /*= true*/
	)
{
	check(NodeClass != nullptr);											 

	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(InParentGraph);
	
	if (ensure(Graph != nullptr))
	{
		UOptimusNode* ModelNode = Graph->GetModelGraph()->AddNode(NodeClass, InLocation);

 		// FIXME: Automatic connection from the given pin.

		UOptimusEditorGraphNode* GraphNode = Graph->FindGraphNodeFromModelNode(ModelNode);
		if (GraphNode && bInSelectNewNode)
		{
			Graph->SelectNodeSet({GraphNode});
		}
		return GraphNode;
	}

	return nullptr;
}
