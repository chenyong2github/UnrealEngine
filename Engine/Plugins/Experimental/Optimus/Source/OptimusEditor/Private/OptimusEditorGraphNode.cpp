// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphNode.h"

#include "OptimusEditorGraphSchema.h"

#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "EdGraphSchema_K2.h"

// FIXME: Move to registration.
namespace OptimusTypeName
{
	static FName Bool(TEXT("bool"));
	static FName Int(TEXT("int32"));
	static FName Float(TEXT("float"));
	static FName String(TEXT("FString"));
	static FName Name(TEXT("FName"));
	static FName MeshCompoennt(TEXT("UMeshComponent*"));
	static FName SkeletalMesh(TEXT("USkeletalMesh*"));	
	static FName StaticMesh(TEXT("UStaticMesh*"));
	static FName MeshAttribute(TEXT("UOptimusMeshAttribute*"));
	static FName MeshSkinWeights(TEXT("UOptimusMeshSkinWeights*"));
	static FName Skeleton(TEXT("USkeleton*"));
}



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


void UOptimusEditorGraphNode::SynchronizeGraphPinValueWithModelPin(UEdGraphPin* InGraphPin)
{
	const UOptimusNodePin* ModelPin = FindModelPinFromGraphPin(InGraphPin);
	if (!ModelPin)
	{
		return;
	}

	// This pin doesn't care about value display.
	if (InGraphPin->bDefaultValueIsIgnored)
	{
		return;
	}

	// If the pin has sub-pins, don't bother.
	if (!ModelPin->GetSubPins().IsEmpty())
	{
		return;
	}

	FString ValueString = ModelPin->GetValueAsString();

	if (InGraphPin->DefaultValue != ValueString)
	{
		InGraphPin->Modify();
		InGraphPin->DefaultValue = ValueString;
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
	FEdGraphPinType PinType;

	FName TypeName = InModelPin->GetTypeName();

	if (TypeName == OptimusTypeName::Bool)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeName == OptimusTypeName::Int)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeName == OptimusTypeName::Float)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (TypeName == OptimusTypeName::String || TypeName == OptimusTypeName::Name)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeName == OptimusTypeName::MeshCompoennt || 
		     TypeName == OptimusTypeName::SkeletalMesh ||
			 TypeName == OptimusTypeName::StaticMesh)
	{
		PinType.PinCategory = OptimusSchemaPinTypes::Mesh;
		PinType.ContainerType = EPinContainerType::Map;
	}
	else if (TypeName == OptimusTypeName::MeshAttribute ||
			 TypeName == OptimusTypeName::MeshSkinWeights)
	{
		PinType.PinCategory = OptimusSchemaPinTypes::Attribute;
		PinType.ContainerType = EPinContainerType::Array;
		PinType.PinSubCategory = TypeName;
	}
	else if (TypeName == OptimusTypeName::Skeleton)
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
