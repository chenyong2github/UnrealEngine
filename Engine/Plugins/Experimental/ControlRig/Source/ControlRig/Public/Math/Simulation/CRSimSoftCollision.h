// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/ControlRigMathLibrary.h"
#include "CRSimPoint.h"
#include "CRSimSoftCollision.generated.h"

UENUM()
enum class ECRSimSoftCollisionType : uint8
{
	Plane,
	Sphere,
	Cone
};

USTRUCT()
struct FCRSimSoftCollision
{
	GENERATED_BODY()

	FCRSimSoftCollision()
	{
		Transform = FTransform::Identity;
		ShapeType = ECRSimSoftCollisionType::Sphere;
		MinimumDistance = 10.f;
		MaximumDistance = 20.f;
		FalloffType = EControlRigAnimEasingType::CubicEaseIn;
		Coefficient = 64.f;
		bInverted = false;
	}

	/**
	 * The world / global transform of the collisoin
	 */
	UPROPERTY()
	FTransform Transform;

	/**
	 * The type of collision shape
	 */
	UPROPERTY()
	ECRSimSoftCollisionType ShapeType;

	/**
	 * The minimum distance for the collision.
	 * If this is equal or higher than the maximum there's no falloff.
	 * For a cone shape this represents the minimum angle in degrees.
	 */
	UPROPERTY()
	float MinimumDistance;

	/**
	 * The maximum distance for the collision.
	 * If this is equal or lower than the minimum there's no falloff.
	 * For a cone shape this represents the maximum angle in degrees.
	 */
	UPROPERTY()
	float MaximumDistance;

	/**
	 * The type of falloff to use
	 */
	UPROPERTY()
	EControlRigAnimEasingType FalloffType;

	/**
	 * The strength of the collision force
	 */
	UPROPERTY()
	float Coefficient;

	/**
	 * If set to true the collision volume will be inverted
	 */
	UPROPERTY()
	bool bInverted;

	static float CalculateFalloff(const FCRSimSoftCollision& InCollision, const FVector& InPosition, float InSize, FVector& OutDirection);
	FVector CalculateForPoint(const FCRSimPoint& InPoint, float InDeltaTime) const;
};
