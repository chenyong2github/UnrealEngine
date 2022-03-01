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
	FVector Direction = FVector::ZeroVector;

	// Target clamped directions will be applied to any source direction within this angle boundary
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ClampMin="1.0", ClampMax="180.0"))
	float AngleTresholdDegrees = 0.f;
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
	* @param bPreserveRotation	If true, sample rotations will be replaced with the present sample rotation
	*
	* @return					Direction clamped, modified trajectory range
	*/
	UFUNCTION(BlueprintPure, Category="Motion Trajectory", meta=(BlueprintThreadSafe, AutoCreateRefTerm="Directions"))
	static FTrajectorySampleRange ClampTrajectoryDirection(
		FTrajectorySampleRange Trajectory, 
		const TArray<FTrajectoryDirectionClamp>& Directions, 
		bool bPreserveRotation = true);

	/**
	* Rotates the trajectory
	*
	* @param Trajectory			Input trajectory range
	* @param Rotation			Rotation to be applied to all trajectory samples
	*
	* @return					Rotated trajectory
	*/
	UFUNCTION(BlueprintPure, Category="Motion Trajectory", meta=(BlueprintThreadSafe))
	static FTrajectorySampleRange RotateTrajectory(FTrajectorySampleRange Trajectory, const FQuat& Rotation);

	/**
	* Rotates the trajectory to make it relative to the component specified as a parameter
	*
	* @param Trajectory			Input trajectory range
	* @param Component			Component whose transform will be the new trajectory reference frame
	*
	* @return					Trajectory relative to Component
	*/
	UFUNCTION(BlueprintPure, Category="Motion Trajectory", meta=(BlueprintThreadSafe))
	static FTrajectorySampleRange MakeTrajectoryRelativeToComponent(
		FTrajectorySampleRange ActorTrajectory, 
		const class USceneComponent* Component);

	/**
	* Projects trajectory samples onto a defined set of allowed directions
	*
	* @param WorldTransform		Input world transform to establish a world-space basis for drawing the trajectory
	* @param Trajectory			Input trajectory range
	* @param PredictionColor	Input prediction color to interpolate sample drawing towards
	* @param HistoryColor		Input historical color to interpolate sample drawing towards
	* @param ArrowScale			Input sample velocity draw scale
	* @param ArrowSize			Input sample arrow draw size
	* @param ArrowThickness		Input sample arrow draw thickness
	* @param bDrawText			Input include drawing of per-sample trajectory information
	*/
	UFUNCTION(BlueprintCallable, Category="Motion Trajectory", meta=(BlueprintThreadSafe, AutoCreateRefTerm="WorldTransform"))
	static void DebugDrawTrajectory(const AActor* Actor
		, const FTransform& WorldTransform
		, const FTrajectorySampleRange& Trajectory
		, const FLinearColor PredictionColor = FLinearColor(0.f, 1.f, 0.f)
		, const FLinearColor HistoryColor = FLinearColor(0.f, 0.f, 1.f)
		, float TransformScale = 10.f
		, float TransformThickness = 2.f
		, float ArrowScale = 0.025f
		, float ArrowSize = 40.f
		, float ArrowThickness = 2.f
	);

	// Returns true if the furthest future sample, at the end of the trajectory, has velocity magnitude below 
	// IdleMaxSpeed and present speed above MoveMinSpeed.
	UFUNCTION(BlueprintPure, Category = "Motion Trajectory", meta = (BlueprintThreadSafe))
	static bool IsStoppingTrajectory(
		const FTrajectorySampleRange& Trajectory, 
		float MoveMinSpeed = 1.0f,
		float IdleMaxSpeed = 0.0f);

	// Returns true if the furthest past sample, at the beginning of the trajectory, has velocity magnitude below 
	// IdleMaxSpeed and present speed above MoveMinSpeed.
	UFUNCTION(BlueprintPure, Category = "Motion Trajectory", meta = (BlueprintThreadSafe))
	static bool IsStartingTrajectory(
		const FTrajectorySampleRange& Trajectory, 
		float MoveMinSpeed = 1.0f,
		float IdleMaxSpeed = 0.0f);

	// Returns true if the furthest future sample, at the end of the trajectory, and the present sample, have a velocity
	// magnitude that is within Tolerance of Speed.
	UFUNCTION(BlueprintPure, Category = "Motion Trajectory", meta = (BlueprintThreadSafe))
	static bool IsConstantSpeedTrajectory(
			const FTrajectorySampleRange& Trajectory,
			float Speed = 0.0f,
			float Tolerance = 0.001f);
};