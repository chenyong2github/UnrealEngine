// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphNode.h"

#include "OptimusEditorGraphSchema.h"

#include "OptimusDataType.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "EdGraphSchema_K2.h"


void UOptimusEditorGraphNode::Construct(UOptimusNode* InModelNode)
{
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

FText UOptimusEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (ModelNode)
	{
		return ModelNode->GetDisplayName();
	}

	return {};
}

void UOptimusEditorGraphNode::CreateGraphPinFromModelPin(
	const UOptimusNodePin* InModelPin,
	EEdGraphPinDirection InDirection,
	UEdGraphPin* InParentPin
)
{
	FOptimusDataTypeHandle DataType = InModelPin->GetDataType();
	if (!ensure(DataType.IsValid()))
	{
		return;
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
