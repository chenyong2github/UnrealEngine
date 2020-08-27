// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphSchema.h"

#include "IOptimusEditor.h"
#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"

#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "Types/OptimusType_MeshAttribute.h"
#include "Types/OptimusType_MeshSkinWeights.h"

#include "EdGraphSchema_K2.h"
#include "Toolkits/ToolkitManager.h"
#include "ScopedTransaction.h"


FName OptimusSchemaPinTypes::Attribute("Optimus_Attribute");
FName OptimusSchemaPinTypes::Skeleton("Optimus_Skeleton");
FName OptimusSchemaPinTypes::Mesh("Optimus_Mesh");

static UOptimusNodePin* GetModelPinFromGraphPin(const UEdGraphPin* InGraphPin)
{
	UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(InGraphPin->GetOwningNode());

	if (ensure(GraphNode != nullptr) && ensure(GraphNode->ModelNode != nullptr))
	{
		return GraphNode->ModelNode->FindPin(InGraphPin->GetName());
	}

	return nullptr;
}



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


bool UOptimusEditorGraphSchema::TryCreateConnection(
	UEdGraphPin* InPinA, 
	UEdGraphPin* InPinB) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif
	
	if (InPinA->Direction == EGPD_Input)
	{
		Swap(InPinA, InPinB);
	}

	// The pins should be in the correct order now.
	UOptimusNodePin *OutputModelPin = GetModelPinFromGraphPin(InPinA);
	UOptimusNodePin* InputModelPin = GetModelPinFromGraphPin(InPinB);

	if (!OutputModelPin->CanCannect(InputModelPin))
	{
		return false;
	}

	UOptimusNodeGraph *Graph = OutputModelPin->GetNode()->GetOwningGraph();
	return Graph->AddLink(OutputModelPin, InputModelPin);
}


const FPinConnectionResponse UOptimusEditorGraphSchema::CanCreateConnection(
	const UEdGraphPin* InPinA, 
	const UEdGraphPin* InPinB
	) const
{
	if (InPinA->Direction == EGPD_Input)
	{
		Swap(InPinA, InPinB);
	}

	// The pins should be in the correct order now.
	UOptimusNodePin* OutputModelPin = GetModelPinFromGraphPin(InPinA);
	UOptimusNodePin* InputModelPin = GetModelPinFromGraphPin(InPinB);

	FString FailureReason;
	bool bCanConnect = OutputModelPin->CanCannect(InputModelPin, &FailureReason);

	return FPinConnectionResponse(
		bCanConnect ? CONNECT_RESPONSE_MAKE :  CONNECT_RESPONSE_DISALLOW,
		FText::FromString(FailureReason));
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

void UOptimusEditorGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, FGraphDisplayInfo& DisplayInfo) const
{
	const UOptimusEditorGraph& EditorGraph = static_cast<const UOptimusEditorGraph&>(Graph);
	DisplayInfo.PlainName = FText::FromString(EditorGraph.GetModelGraph()->GetName());
	DisplayInfo.DisplayName = DisplayInfo.PlainName;
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


void UOptimusEditorGraphSchema::TrySetDefaultValue(
	UEdGraphPin& Pin, 
	const FString& NewDefaultValue, 
	bool bMarkAsModified
	) const
{
	UOptimusNodePin* ModelPin = GetModelPinFromGraphPin(&Pin);
	if (ensure(ModelPin))
	{
		// Kill the existing transaction, since it copies the wrong node.
		GEditor->CancelTransaction(0);

		ModelPin->SetValueFromString(NewDefaultValue);
	}
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

static FText GetGraphSubCategory(UOptimusNodeGraph* InGraph)
{
	if (InGraph->GetGraphType() == EOptimusNodeGraphType::ExternalTrigger)
	{
		return FText::FromString(TEXT("Triggered Graphs"));
	}
	else
	{
		return FText::GetEmpty();
	}
}

static FText GetGraphTooltip(UOptimusNodeGraph* InGraph)
{
	return FText::GetEmpty();
}


FOptimusSchemaAction_Graph::FOptimusSchemaAction_Graph(
	UOptimusNodeGraph* InGraph,
	int32 InGrouping) : 
		FEdGraphSchemaAction(
			GetGraphSubCategory(InGraph), 
			FText::FromString(InGraph->GetName()), 
			GetGraphTooltip(InGraph), 
			InGrouping, 
			FText(), 
			int32(EOptimusSchemaItemGroup::Graphs) 
		), 
		GraphType(InGraph->GetGraphType())
{
	GraphPath = InGraph->GetGraphPath();
}
