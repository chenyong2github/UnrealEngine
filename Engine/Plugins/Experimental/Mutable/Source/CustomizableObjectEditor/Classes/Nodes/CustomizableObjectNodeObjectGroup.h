// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNode.h"
#include "CustomizableObjectNodeObjectGroup.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeObjectGroup : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeObjectGroup();

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	FString GroupName;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	ECustomizableObjectGroupType GroupType;

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "Parameter UI Metadata"))
	FMutableParamUIMetadata ParamUIMetadata;

	// UObject interface.
	void Serialize(FArchive& Ar) override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// Own interface
	UEdGraphPin* ObjectsPin() const
	{
		return FindPin(TEXT("Objects"));
	}

	UEdGraphPin* GroupProjectorsPin() const
	{
		return FindPin(TEXT("Projectors"));
	}
};

