// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "CoreMinimal.h"

#include "OptimusMeshSkinWeights.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusMeshSkinWeights : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName SkinWeightProfileName;
};
