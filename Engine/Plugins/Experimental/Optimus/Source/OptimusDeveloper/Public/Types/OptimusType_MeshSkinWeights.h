// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "CoreMinimal.h"

#include "OptimusType_MeshSkinWeights.generated.h"

UCLASS()
class OPTIMUSDEVELOPER_API UOptimusType_MeshSkinWeights : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName SkinWeightProfileName;
};
