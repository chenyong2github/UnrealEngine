// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNodeEditMaterialBase.h"

#include "CustomizableObjectNodeMorphMaterial.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMorphMaterial : public UCustomizableObjectNodeEditMaterialBase
{
public:

	GENERATED_BODY()
	
	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup() override;
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	bool IsNodeOutDatedAndNeedsRefresh() override;
	FString GetRefreshMessage() const override;

	UPROPERTY(Category=CustomizableObject, EditAnywhere)
	FString MorphTargetName;

	bool IsSingleOutputNode() const override;

	UEdGraphPin* FactorPin() const
	{
		return FindPin(TEXT("Factor"));
	}
};

