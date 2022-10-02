// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

#include "CustomizableObjectNodeMeshReshapeCommon.generated.h"

USTRUCT()
struct FMeshReshapeBoneReference
{
	GENERATED_USTRUCT_BODY()

	/** Name of the bone that will be deformed */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FName BoneName;
};
