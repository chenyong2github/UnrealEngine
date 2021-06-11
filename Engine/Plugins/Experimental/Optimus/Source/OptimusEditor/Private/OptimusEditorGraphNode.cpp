// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphNode.h"

#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphSchema.h"

#include "OptimusDataType.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "EdGraphSchema_K2.h"
#include "GraphEditAction.h"


void UOptimusEditorGraphNode::Construct(UOptimusNode* InModelNode)
{
	// Our graph nodes are not transactional. We handle the transacting ourselves.
	ClearFlags(RF_Transactional);
	
	if (ensure(InModelNode))
	{
		ModelNode = InModelNode;

		NodePosX = int(InModelNode->GetGraphPosition().X);
		NodePosY = int(InModelNode->GetGraphPosition().Y);

		UpdateTopLevelPins();

		// Start with all input pins
		for (const UOptimusNodePin* ModelPin : ModelNode->GetPins())
		{
			if (ModelPin->GetDirection() == EOptimusNodePinDirection::Input)
			{
				CreateGraphPinFromModelPin(ModelPin, EGPD_Input);
			}
		}

		// Then all output pins
		for (const UOptimusNodePin* ModelPin : ModelNode->GetPins())
		{
			if (ModelPin->GetDirection() == EOptimusNodePinDirection::Output)
			{
				CreateGraphPinFromModelPin(ModelPin, EGPD_Output);
			}
		}
	}
}


UOptimusNodePin* UOptimusEditorGraphNode::FindModelPinFromGraphPin(
	const UEdGraphPin* InGraphPin
	)
{
	if (InGraphPin == nullptr)
	{
		return nullptr;
	}
	return PathToModelPinMap.FindRef(InGraphPin->GetFName());
}

UEdGraphPin* UOptimusEditorGraphNode::FindGraphPinFromModelPin(
	const UOptimusNodePin* InModelPin
)
{
	if (InModelPin == nullptr)
	{
		return nullptr;
	}
	return PathToGraphPinMap.FindRef(InModelPin->GetUniqueName());
}


void UOptimusEditorGraphNode::SynchronizeGraphPinNameWithModelPin(
	const UOptimusNodePin* InModelPin
	)
{
	// FindGraphPinFromModelPin will not work here since the model pin now carries the new pin
	// path but our maps do not. We have to search linearly on the pointer to get the old name.
	FName OldPinPath;
	for (const TPair<FName, UOptimusNodePin*>& Item : PathToModelPinMap)
	{
		if (Item.Value == InModelPin)
		{
			OldPinPath = Item.Key;
			break;
		}
	}

	UEdGraphPin* GraphPin = nullptr;

	if (!OldPinPath.IsNone())
	{
		GraphPin = PathToGraphPinMap.FindRef(OldPinPath);
	}

	if (GraphPin)
	{
		FName NewPinPath = InModelPin->GetUniqueName();

		// Update the resolver maps first.
		PathToModelPinMap.Remove(OldPinPath);
		PathToModelPinMap.Add(NewPinPath, const_cast<UOptimusNodePin *>(InModelPin));

		PathToGraphPinMap.Remove(OldPinPath);
		PathToGraphPinMap.Add(NewPinPath, GraphPin);

		GraphPin->PinName = NewPinPath;
		GraphPin->PinFriendlyName = InModelPin->GetDisplayName();

		for (UOptimusNodePin* ModelSubPin : InModelPin->GetSubPins())
		{
			SynchronizeGraphPinNameWithModelPin(ModelSubPin);
		}
	}

	// The slate node will automatically pick up the new name on the next tick.
}


void UOptimusEditorGraphNode::SynchronizeGraphPinValueWithModelPin(
	const UOptimusNodePin* InModelPin
	)
{
	if (ensure(InModelPin))
	{
		if (InModelPin->GetSubPins().IsEmpty())
		{
			UEdGraphPin *GraphPin = FindGraphPinFromModelPin(InModelPin);

			// Only update the value if the pin cares about it.
			if (ensure(GraphPin) && !GraphPin->bDefaultValueIsIgnored)
			{
				FString ValueString = InModelPin->GetValueAsString();

				if (GraphPin->DefaultValue != ValueString)
				{
					GraphPin->Modify();
					GraphPin->DefaultValue = ValueString;
				}
			}
		}
		else
		{
			for (const UOptimusNodePin* ModelSubPin : InModelPin->GetSubPins())
			{
				SynchronizeGraphPinValueWithModelPin(ModelSubPin);
			}
		}
	}
}


void UOptimusEditorGraphNode::SynchronizeGraphPinTypeWithModelPin(
	const UOptimusNodePin* InModelPin
	)
{
	FOptimusDataTypeHandle DataType = InModelPin->GetDataType();
	if (!ensure(DataType.IsValid()))
	{
		return;
	}

	FEdGraphPinType PinType = UOptimusEditorGraphSchema::GetPinTypeFromDataType(DataType);

	// If the pin has sub-pins, we need to reconstruct.
	UEdGraphPin* GraphPin = FindGraphPinFromModelPin(InModelPin);
	if (!GraphPin)
	{
		// TBD: Does it need to exist?
		return;
	}

	// If the graph node had sub-pins, we need to remove those.
	if (!GraphPin->SubPins.IsEmpty())
	{
		RemoveGraphSubPins(GraphPin);
		GraphPin->Purge();
	}

	if (!InModelPin->GetSubPins().IsEmpty())
	{
		for (const UOptimusNodePin* ModelSubPin : InModelPin->GetSubPins())
		{
			CreateGraphPinFromModelPin(ModelSubPin, GraphPin->Direction, GraphPin);
		}
	}
		
	GraphPin->PinType = PinType;

	Cast<UOptimusEditorGraph>(GetGraph())->RefreshVisualNode(this);
}


FText UOptimusEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (ModelNode)
	{
		return ModelNode->GetDisplayName();
	}

	return {};
}

bool UOptimusEditorGraphNode::CreateGraphPinFromModelPin(
	const UOptimusNodePin* InModelPin,
	EEdGraphPinDirection InDirection,
	UEdGraphPin* InParentPin
)
{
	FOptimusDataTypeHandle DataType = InModelPin->GetDataType();
	if (!ensure(DataType.IsValid()))
	{
		return false;
	}

	FEdGraphPinType PinType = UOptimusEditorGraphSchema::GetPinTypeFromDataType(DataType);

	FName PinPath = InModelPin->GetUniqueName();
	UEdGraphPin *GraphPin = CreatePin(InDirection, PinType, PinPath);

	GraphPin->PinFriendlyName = InModelPin->GetDisplayName();

	if (InParentPin)
	{
		InParentPin->SubPins.Add(GraphPin);
		GraphPin->ParentPin = InParentPin;
	}

	// Maintain a mapping from the pin path, which is also the graph pin's internal name, 
	// to the original model pin.
	PathToModelPinMap.Add(PinPath, const_cast<UOptimusNodePin *>(InModelPin));
	PathToGraphPinMap.Add(PinPath, GraphPin);

	if (InModelPin->GetSubPins().IsEmpty())
	{
		GraphPin->DefaultValue = InModelPin->GetValueAsString();
	}
	else
	{
		for (const UOptimusNodePin* ModelSubPin : InModelPin->GetSubPins())
		{
			CreateGraphPinFromModelPin(ModelSubPin, InDirection, GraphPin);
		}
	}
	return true;
}

void UOptimusEditorGraphNode::RemoveGraphSubPins(
	UEdGraphPin* InParentPin,
	bool bInIsRootPinToDelete
	)
{
	// Make a copy of the subpins, because calling MarkPendingKill removes it from the subpin
	// list of the parent.
	TArray<UEdGraphPin*> SubPins = InParentPin->SubPins;
	
	for (UEdGraphPin* SubPin: SubPins)
	{
		PathToModelPinMap.Remove(SubPin->PinName);
		PathToGraphPinMap.Remove(SubPin->PinName);

		// Remove this pin from our owned pins
		Pins.Remove(SubPin);

		if (!SubPin->SubPins.IsEmpty())
		{
			RemoveGraphSubPins(SubPin, false);
		}

		if (bInIsRootPinToDelete)
		{
			SubPin->MarkPendingKill();
		}
	}
}


bool UOptimusEditorGraphNode::ModelPinAdded(const UOptimusNodePin* InModelPin)
{
	EEdGraphPinDirection GraphPinDirection;

	if (InModelPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		GraphPinDirection = EGPD_Input;
	}
	else if (InModelPin->GetDirection() == EOptimusNodePinDirection::Output)
	{
		GraphPinDirection = EGPD_Output;
	}
	else
	{
		return false;
	}
	
	if (!CreateGraphPinFromModelPin(InModelPin, GraphPinDirection))
	{
		return false;
	}

	UpdateTopLevelPins();

	return true;
}


bool UOptimusEditorGraphNode::ModelPinRemoved(const UOptimusNodePin* InModelPin)
{
	// TBD
	check(false);
	return false;
}


void UOptimusEditorGraphNode::UpdateTopLevelPins()
{
	TopLevelInputPins.Empty();
	TopLevelOutputPins.Empty();

	if (ensure(ModelNode))
	{
		for (UOptimusNodePin* Pin : ModelNode->GetPins())
		{
			if (Pin->GetDirection() == EOptimusNodePinDirection::Input)
			{
				TopLevelInputPins.Add(Pin);
			}
			else if (Pin->GetDirection() == EOptimusNodePinDirection::Output)
			{
				TopLevelOutputPins.Add(Pin);			
			}
		}
	}
}
