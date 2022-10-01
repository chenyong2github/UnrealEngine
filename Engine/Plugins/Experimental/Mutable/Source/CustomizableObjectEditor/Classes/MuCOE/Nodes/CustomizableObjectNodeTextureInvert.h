// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeTextureInvert.generated.h"

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureInvert : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()
	
	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TittleType)const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	virtual void Serialize(FArchive& Ar) override;
	
	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* GetBaseImagePin() const;

private:
	UPROPERTY()
	FEdGraphPinReference BaseImagePinReference;
};
