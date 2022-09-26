// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNodeEditMaterialBase.h"

#include "CustomizableObjectNodeEditLayoutBlocks.generated.h"


UCLASS(Abstract)
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeEditLayoutBlocks : public UCustomizableObjectNodeEditMaterialBase
{
public:
	GENERATED_BODY()

	// Selected blocks
	UPROPERTY()
	TArray<FGuid> BlockIds;

};

