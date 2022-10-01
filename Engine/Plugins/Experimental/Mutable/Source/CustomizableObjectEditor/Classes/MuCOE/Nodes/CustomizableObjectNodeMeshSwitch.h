// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"

#include "EdGraph/EdGraphPin.h"

#include "CustomizableObjectNodeMeshSwitch.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMeshSwitch : public UCustomizableObjectNodeSwitchBase
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
