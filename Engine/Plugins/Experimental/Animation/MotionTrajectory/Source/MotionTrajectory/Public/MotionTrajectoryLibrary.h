// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MotionTrajectoryTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"

#include "MotionTrajectoryLibrary.generated.h"

USTRUCT(BlueprintType, Category="Motion Trajectory")
struct MOTIONTRAJECTORY_API FTrajectoryDirectionClamp
{
	GENERATED_BODY()

	// Target clamped direction for an incoming source direction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	FVector Direction;

	// Target clamped directions will be applied to any source direction within this angle boundary
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ClampMin="1.0", ClampMax="180.0"))
	float AngleTresholdDegrees;
};

UCLASS(Category="Motion Trajectory")
class MOTIONTRAJECTORY_API UMotionTrajectoryBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	* Removes the Z axis motion contribution from a trajectory range
	*
	* @param Trajectory			Input trajectory range
	* @param PreserveSpeed		Input bool determining if velocity magnitude is preserved (true) or projected (false)
	*
	* @return					Z axis flattened, modified trajectory range
	*/
	UFUNCTION(BlueprintPure, Category="Motion Trajectory", meta=(BlueprintThreadSafe))
	static FTrajectorySampleRange FlattenTrajectory2D(FTrajectorySampleRange Trajectory, bool PreserveSpeed = true);

	/**
	* Projects trajectory samples onto a defined set of allowed directions
	*
	* @param Trajectory			Input trajectory range
	* @param Directions			Input direction clamping, containing angle thresholds for determining source to target direction
	*
	* @return					Direction clamped, modified trajectory range
	*/
	UFUNCTION(BlueprintPure, Category = "Motion Trajectory", meta=(BlueprintThreadSafe, AutoCreateRefTerm="Directions"))
	static FTrajectorySampleRange ClampTrajectoryDirection(FTrajectorySampleRange Trajectory, const TArray<FTrajectoryDirectionClamp>& Directions);
};