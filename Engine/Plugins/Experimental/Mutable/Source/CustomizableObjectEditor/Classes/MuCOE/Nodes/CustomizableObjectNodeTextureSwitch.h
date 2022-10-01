// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"

#include "EdGraph/EdGraphPin.h"

#include "CustomizableObjectNodeTextureSwitch.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTextureSwitch : public UCustomizableObjectNodeSwitchBase
{
public:
	GENERATED_BODY()

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;

	// UCustomizableObjectNodeSwitchBase interface
	FString GetOutputPinName() const override;

	FName GetCategory() const override;

	FString GetPinPrefix() const;
};
