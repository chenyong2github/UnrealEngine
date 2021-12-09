// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphSchema.h"

#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphSchemaActions.h"
#include "OptimusEditorGraphNode.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"

#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusEditorGraphConnectionDrawingPolicy.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "OptimusEditor"

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
	// Basic Nodes
	for (UClass* Class : UOptimusNode::GetAllNodeClasses())
	{
		UOptimusNode* Node = Cast<UOptimusNode>(Class->GetDefaultObject());
		if (Node == nullptr)
		{
			continue;
		}

		const FText NodeName = Node->GetDisplayName();
		const FText NodeCategory = FText::FromName(Node->GetNodeCategory());

		TSharedPtr< FOptimusGraphSchemaAction_NewNode> Action(
			new FOptimusGraphSchemaAction_NewNode(
				NodeCategory,
				NodeName,
				/* Tooltip */{}, 0, /* Keywords */{}
		));

		Action->NodeClass = Class;

		IoActionBuilder.AddAction(Action);
	}

	// Constant Value Nodes
	for (FOptimusDataTypeHandle DataTypeHandle: FOptimusDataTypeRegistry::Get().GetAllTypes())
	{
		if (DataTypeHandle->CanCreateProperty())
		{
			const FText NodeName = FText::Format(LOCTEXT("ConstantValueNode", "{0} Constant"), DataTypeHandle->DisplayName);
			const FText NodeCategory = FText::FromName(UOptimusNode::CategoryName::Values);
			
			TSharedPtr< FOptimusGraphSchemaAction_NewConstantValueNode> Action(
				new FOptimusGraphSchemaAction_NewConstantValueNode(
					NodeCategory,
					NodeName,
					/* Tooltip */{}, 0, /* Keywords */{}
			));

			Action->DataType = DataTypeHandle;

			IoActionBuilder.AddAction(Action);
		}
	}

	// Data Interface Nodes
	for (UClass* Class : UOptimusComputeDataInterface::GetAllComputeDataInterfaceClasses())
	{
		UOptimusComputeDataInterface* DataInterface = Cast<UOptimusComputeDataInterface>(Class->GetDefaultObject());
		if (!ensure(DataInterface != nullptr))
		{
			continue;
		}

		const FText NodeName = FText::FromString(DataInterface->GetDisplayName());

		// FIXME: Get from interface
		const FText NodeCategory = FText::FromName(UOptimusNode::CategoryName::DataProviders);

		TSharedPtr< FOptimusGraphSchemaAction_NewDataInterfaceNode> Action(
			new FOptimusGraphSchemaAction_NewDataInterfaceNode(
				NodeCategory,
				NodeName,
				/* Tooltip */{}, 0, /* Keywords */{}
		));

		Action->DataInterfaceClass = Class;

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

	UOptimusNodeGraph *Graph = OutputModelPin->GetOwningNode()->GetOwningGraph();
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
	UOptimusEditorGraph* EditorGraph = Cast<UOptimusEditorGraph>(GraphNode->GetGraph());

	if (ensure(EditorGraph))
	{
		UOptimusNodeGraph* ModelGraph = EditorGraph->GetModelGraph();

		UOptimusNodePin *TargetModelPin = GraphNode->FindModelPinFromGraphPin(&TargetPin);

		if (ensure(TargetModelPin))
		{
			ModelGraph->RemoveAllLinks(TargetModelPin);
		}
	}
}


void UOptimusEditorGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{

}


FConnectionDrawingPolicy* UOptimusEditorGraphSchema::CreateConnectionDrawingPolicy(
	int32 InBackLayerID,
	int32 InFrontLayerID,
	float InZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj
	) const
{
	return new FOptimusEditorGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
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


FEdGraphPinType UOptimusEditorGraphSchema::GetPinTypeFromDataType(
	FOptimusDataTypeHandle InDataType
	)
{
	FEdGraphPinType PinType;

	if (InDataType.IsValid())
	{
		// Set the categories as defined by the registered data type. We hijack the PinSubCategory
		// so that we can query back to the registry for whether the pin color should come out of the
		// K2 schema or the registered custom color.
		PinType.PinCategory = InDataType->TypeCategory;
		PinType.PinSubCategory = InDataType->TypeName;
		PinType.PinSubCategoryObject = InDataType->TypeObject;
	}

	return PinType;
}



FLinearColor UOptimusEditorGraphSchema::GetPinTypeColor(
	const FEdGraphPinType& InPinType
	) const
{
	return GetColorFromPinType(InPinType);
}


const FSlateBrush* UOptimusEditorGraphSchema::GetIconFromPinType(
	const FEdGraphPinType& InPinType
	)
{
	const FSlateBrush* IconBrush = FEditorStyle::GetBrush(TEXT("Kismet.VariableList.TypeIcon"));
	const UObject *TypeObject = InPinType.PinSubCategoryObject.Get();

	if (TypeObject)
	{
		UClass *VarClass = FindObject<UClass>(ANY_PACKAGE, *TypeObject->GetName());
		if (VarClass)
		{
			IconBrush = FSlateIconFinder::FindIconBrushForClass(VarClass);
		}
	}

	return IconBrush;
}


FLinearColor UOptimusEditorGraphSchema::GetColorFromPinType(const FEdGraphPinType& InPinType)
{
	// Use the PinSubCategory value to resolve the type. It's set in
	// UOptimusEditorGraphSchema::GetPinTypeFromDataType.
	FOptimusDataTypeHandle DataType = FOptimusDataTypeRegistry::Get().FindType(InPinType.PinSubCategory);

	// Unset data types get black pins.
	if (!DataType.IsValid())
	{
		return FLinearColor::Black;
	}

	// If the data type has custom color, use that. Otherwise fall back on the K2 schema
	// since we want to be compatible with known types (which also have preferences for them).
	if (DataType->bHasCustomPinColor)
	{
		return DataType->CustomPinColor;
	}

	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(InPinType);
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

#undef LOCTEXT_NAMESPACE