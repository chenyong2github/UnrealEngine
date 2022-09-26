// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNode.h"

#include "Engine/Texture2D.h"

#include "CustomizableObjectNodeTextureParameter.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureParameter : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TObjectPtr<UTexture2D> DefaultValue;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	FString ParameterName = "Default Name";

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "Parameter UI Metadata"))
	FMutableParamUIMetadata ParamUIMetadata;

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool IsAffectedByLOD() const override { return false; }
};

