// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGPoint.generated.h"

USTRUCT(BlueprintType)
struct FPCGPoint
{
	GENERATED_BODY()
public:
	FPCGPoint() = default;

	FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed);

	FBoxSphereBounds GetBounds() const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FTransform Transform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FVector Extents;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	float Density;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	int32 Seed;
};