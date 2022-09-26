// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNodeSwitchBase.h"

#include "EdGraph/EdGraphPin.h"

#include "CustomizableObjectNodeFloatSwitch.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeFloatSwitch : public UCustomizableObjectNodeSwitchBase
{
public:
	GENERATED_BODY()

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	
	// UCustomizableObjectNodeSwitchBase interface
	FString GetOutputPinName() const override;

	FName GetCategory() const override;

	// Own interface
	FString GetPinPrefix() const;
};
