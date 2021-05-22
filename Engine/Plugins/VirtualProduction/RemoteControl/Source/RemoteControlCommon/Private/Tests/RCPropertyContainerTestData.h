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
	bool bSomeBool;
	
	UPROPERTY()
	uint32 SomeUInt32;

	UPROPERTY()
	float SomeFloat;

	UPROPERTY()
	FVector SomeVector;

	UPROPERTY()
	FRotator SomeRotator;

	UPROPERTY(meta = (ClampMin = 20, ClampMax = 145))
	int32 SomeClampedInt = 5;

	UPROPERTY(meta = (ClampMin = 0.2f, ClampMax = 0.92f))
	float SomeClampedFloat = 0.25f;
	
	UPropertyContainerTestObject()
		: SomeUInt32(44),
		SomeFloat(45.0f),
		SomeVector(FVector(0.2f, 0.3f, 0.6f)),
		SomeRotator(FRotator::ZeroRotator) { }
};
