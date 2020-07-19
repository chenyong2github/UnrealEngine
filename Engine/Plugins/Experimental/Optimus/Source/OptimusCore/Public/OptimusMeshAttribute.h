// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "CoreMinimal.h"

#include "OptimusMeshAttribute.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusMeshAttribute : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName AttributeName;
};
