// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "RCPropertyContainerTestData.generated.h"

UCLASS()
class UPropertyContainerTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint32 SomeUInt32;

	UPROPERTY()
	float SomeFloat;

	UPROPERTY()
	FVector SomeVector;

	UPROPERTY(meta = (ClampMin = -5.0f, ClampMax = 99.0f))
	float ClampedFloat = 56.0f;
	
	UPropertyContainerTestObject()
		: SomeUInt32(44),
		SomeFloat(45.0f),
		SomeVector(FVector(0.2f, 0.3f, 0.6f)) { }
};
