// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsPublic.h"
#include "Math/MathFwd.h"

#include "ChaosEventType.generated.h"


namespace Chaos
{
	struct FBreakingData;
}

USTRUCT(BlueprintType)
struct ENGINE_API FBreakChaosEvent
{
	GENERATED_BODY()

public:

	FBreakChaosEvent();
	FBreakChaosEvent(const Chaos::FBreakingData& BreakingData);

	/** primitive component involved in the break event */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	TObjectPtr<UPrimitiveComponent> Component = nullptr;

	/** World location of the break */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector Location;

	/** Linear Velocity of the breaking particle  */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector Velocity;

	/** Angular Velocity of the breaking particle  */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector AngularVelocity;

	/** Extents of the bounding box */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector Extents;

	/** Mass of the breaking particle  */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	float Mass;

	/** Index of the geometry collection bone if positive */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	int32 Index;

	/** Whether the break event originated from a crumble event */
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	bool bFromCrumble;
};

