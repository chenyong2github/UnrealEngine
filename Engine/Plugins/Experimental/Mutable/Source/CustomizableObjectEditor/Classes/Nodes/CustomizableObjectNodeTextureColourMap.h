// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeTextureColourMap.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureColourMap : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* GetMapPin() const
	{
		return FindPin(TEXT("Map"));
	}

	UEdGraphPin* GetBasePin() const
	{
		return FindPin(TEXT("Base"));
	}

	UEdGraphPin* GetMaskPin() const
	{
		return FindPin(TEXT("Mask"));
	}

};
