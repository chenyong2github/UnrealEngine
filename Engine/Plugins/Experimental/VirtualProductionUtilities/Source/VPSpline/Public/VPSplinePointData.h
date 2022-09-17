// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "VPSplinePointData.generated.h"

/* Simple struct to hold spline point data*/
USTRUCT(BlueprintType)
struct FVPSplinePointData
{
	GENERATED_BODY()
public:
	FVPSplinePointData()
		: FocalLength(35.0f)
		, Aperture(2.8f)
		, FocusDistance(100000.f)
	{};

	UPROPERTY(BlueprintReadWrite, Category="VPSpline")
	FVector Location;

	UPROPERTY(BlueprintReadWrite, Category = "VPSpline")
	FRotator Rotation;

	UPROPERTY(BlueprintReadWrite, Category = "VPSpline")
	float FocalLength;

	UPROPERTY(BlueprintReadWrite, Category = "VPSpline")
	float Aperture;

	UPROPERTY(BlueprintReadWrite, Category = "VPSpline")
	float FocusDistance;
};