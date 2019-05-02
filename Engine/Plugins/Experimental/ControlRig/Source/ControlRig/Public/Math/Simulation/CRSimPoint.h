// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CRSimPoint.generated.h"

USTRUCT()
struct FCRSimPoint
{
	GENERATED_BODY()

	FCRSimPoint()
	{
		Mass = 1.f;
		LinearDamping = 0.01f;
		Position = LinearVelocity = FVector::ZeroVector;
	}

	/**
	 * The mass of the point
	 */
	UPROPERTY()
	float Mass;

	/**
	 * The linear damping of the point
	 */
	UPROPERTY()
	float LinearDamping;

	/**
	 * The position of the point
	 */
	UPROPERTY()
	FVector Position;

	/**
	 * The velocity of the point per second
	 */
	UPROPERTY()
	FVector LinearVelocity;

	FCRSimPoint IntegrateVerlet(const FVector& InForce, float InBlend, float InDeltaTime) const;
	FCRSimPoint IntegrateSemiExplicitEuler(const FVector& InForce, float InDeltaTime) const;
};
