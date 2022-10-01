// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeTextureFromColor.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureFromColor : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* ColorPin() const
	{
		return FindPin(TEXT("Color"));
	}
};

