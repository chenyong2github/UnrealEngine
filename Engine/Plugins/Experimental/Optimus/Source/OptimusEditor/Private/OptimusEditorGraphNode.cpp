// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphNode.h"

#include "OptimusEditorGraphSchema.h"

#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "EdGraphSchema_K2.h"

void UOptimusEditorGraphNode::Construct(UOptimusNode* InModelNode)
{
	check(InModelNode);

	ModelNode = InModelNode;

	NodePosX = int(InModelNode->GetGraphPosition().X);
	NodePosY = int(InModelNode->GetGraphPosition().Y);

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
	FEdGraphPinType PinType;

	FString ModelPinType = InModelPin->GetTypeString();

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("ModelType: [%s]"), *ModelPinType);

	if (ModelPinType == TEXT("bool"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (ModelPinType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (ModelPinType == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (ModelPinType == TEXT("FString") || ModelPinType == TEXT("FName"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (ModelPinType == TEXT("UMeshComponent*") || 
		     ModelPinType == TEXT("USkeletalMesh*") ||
			 ModelPinType == TEXT("UStaticMesh*"))
	{
		PinType.PinCategory = OptimusSchemaPinTypes::Mesh;
		PinType.ContainerType = EPinContainerType::Map;
	}
	else if (ModelPinType == TEXT("UOptimusMeshAttribute*") ||
			 ModelPinType == TEXT("UOptimusMeshSkinWeights*"))
	{
		PinType.PinCategory = OptimusSchemaPinTypes::Attribute;
		PinType.ContainerType = EPinContainerType::Array;
		PinType.PinSubCategory = *ModelPinType;
	}
	else if (ModelPinType == TEXT("USkeleton*"))
	{
		PinType.PinCategory = OptimusSchemaPinTypes::Skeleton;
		PinType.ContainerType = EPinContainerType::Set;
		PinType.PinSubCategoryObject = InModelPin->GetTypeObject();
	}
	else if (InModelPin->GetTypeObject() != nullptr)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = InModelPin->GetTypeObject();
	}

	FName PinPath = InModelPin->GetUniqueName();
	UEdGraphPin *GraphPin = CreatePin(InDirection, PinType, PinPath);

	GraphPin->PinFriendlyName = FText::FromName(InModelPin->GetFName());

	if (InParentPin)
	{
		InParentPin->SubPins.Add(GraphPin);
		GraphPin->ParentPin = InParentPin;
	}

	// Maintain a mapping from the pin path, which is also the graph pin's internal name, 
	// to the original model pin.
	PathToModelPinMap.Add(PinPath, const_cast<UOptimusNodePin *>(InModelPin));
	PathToGraphPinMap.Add(PinPath, GraphPin);

	for (const UOptimusNodePin* ModelSubPin : InModelPin->GetSubPins())
	{
		CreateGraphPinFromModelPin(ModelSubPin, InDirection, GraphPin);
	}
}
