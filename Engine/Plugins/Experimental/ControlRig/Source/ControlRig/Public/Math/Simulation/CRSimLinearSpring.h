// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CRSimPoint.h"
#include "CRSimLinearSpring.generated.h"

USTRUCT()
struct FCRSimLinearSpring
{
	GENERATED_BODY()

	FCRSimLinearSpring()
	{
		SubjectA = SubjectB = INDEX_NONE;
		AnchorA = AnchorB = FVector::ZeroVector;
		Coefficient = 32.f;
		Equilibrium = 100.f;
	}

	/**
	 * The first point affected by this spring
	 */
	UPROPERTY()
	int32 SubjectA;

	/**
	 * The second point affected by this spring
	 */
	UPROPERTY()
	int32 SubjectB;

	/**
	 * The anchor in the space of the first subject
	 */
	UPROPERTY()
	FVector AnchorA;

	/**
	 * The anchor in the space of the second subject
	 */
	UPROPERTY()
	FVector AnchorB;

	/**
	 * The power of this spring
	 */
	UPROPERTY()
	float Coefficient;

	/**
	 * The rest length of this spring
	 */
	UPROPERTY()
	float Equilibrium;

	void CalculateForPoints(const FCRSimPoint& InPointA, const FCRSimPoint& InPointB, FVector& ForceA, FVector& ForceB) const;
};
