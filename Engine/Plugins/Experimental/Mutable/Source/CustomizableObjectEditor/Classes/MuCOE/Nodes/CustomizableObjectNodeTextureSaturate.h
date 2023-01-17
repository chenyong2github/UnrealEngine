// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "UObject/UObjectGlobals.h"

#include "CustomizableObjectNodeTextureSaturate.generated.h"

class FArchive;
class UCustomizableObjectNodeRemapPins;
class UObject;

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureSaturate : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TittleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	
	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* GetBaseImagePin() const;
	UEdGraphPin* GetFactorPin() const;

private:
	UPROPERTY()
	FEdGraphPinReference BaseImagePinReference;


	UPROPERTY()
	FEdGraphPinReference FactorPinReference;
};
