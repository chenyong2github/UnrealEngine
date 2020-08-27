// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "CoreMinimal.h"

#include "OptimusType_MeshAttribute.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusType_MeshAttribute : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName AttributeName;
};
