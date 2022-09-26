// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/SCustomizableObjectNodeMaterial.h"
#include "EdGraphSchema_CustomizableObject.h"
#include "Nodes/SCustomizableObjectNodeMaterialPinImage.h"


void SCustomizableObjectNodeMaterial::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
	GraphNode = InGraphNode;
	UpdateGraphNode();
}


TSharedPtr<SGraphPin> SCustomizableObjectNodeMaterial::CreatePinWidget(UEdGraphPin* Pin) const
{
	if (Pin->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Image &&
		Pin->Direction == EGPD_Input)
	{
		return SNew(SCustomizableObjectNodeMaterialPinImage, Pin);
	}

	return SGraphNode::CreatePinWidget(Pin);
}
