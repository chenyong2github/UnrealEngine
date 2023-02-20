// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MotionTrajectoryTypes.h"
#include "Components/ActorComponent.h"
#include "PoseSearch/PoseSearchTrajectoryTypes.h"

// This is temporary to support compatibility with existing data until we switch Motion Matching to use FPoseSearchTrajectory.
#include "Animation/MotionTrajectoryTypes.h"

#include "CharacterTrajectoryComponent.generated.h"

class UCharacterMovementComponent;

// Component for generating trajectories usable by Motion Matching. This component generates trajectories from ACharacter.
// This is intended to provide an example and starting point for using Motion Matching with a common setup using the default UCharacterMovementComponent.
// It is expected work flow to extend or replace this component for projects that use a custom movement component or custom movement modes.
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent), Experimental)
class MOTIONTRAJECTORY_API UCharacterTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	
	UCharacterTrajectoryComponent(const FObjectInitializer& ObjectInitializer);

	// Begin UActorComponent Interface
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	// End UActorComponent Interface

protected:
	UFUNCTION()
	void OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);

	void UpdateHistory(float DeltaSeconds, const FTransform& DeltaTransformCS);
	void UpdatePrediction(const FVector& VelocityCS, const FVector& AccelerationCS);

protected:

	// Trajectory stored in component space so it can be directly passed to Motion Matching.
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory")
	FPoseSearchQueryTrajectory Trajectory;

	// This should generally match the longest history required by a Motion Matching Database in the project.
	// Motion Matching will use extrapolation to generate samples if the history doesn't contain enough samples.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trajectory Settings")
	float HistoryLengthSeconds = 1.f;

	// Higher values will cost more storage and processing time, but give higher accuracy.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trajectory Settings", meta = (ClampMin = "1", ClampMax = "120"))
	int32 HistorySamplesPerSecond = 15;

	// This should match the longest trajectory prediction required by a Motion Matching Database in the project.
	// Motion Matching will use extrapolation to generate samples if the prediction doesn't contain enough samples.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trajectory Settings")
	float PredictionLengthSeconds = 1.f;

	// Higher values will cost more storage and processing time, but give higher accuracy.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trajectory Settings", meta = (ClampMin = "1", ClampMax = "120"))
	int32 PredictionSamplesPerSecond = 15;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> SkelMeshComponent;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent;

	int32 NumHistorySamples = -1;
	float SecondsPerHistorySample = 0.f;
	float SecondsPerPredictionSample = 0.f;

	// Current transform of the skeletal mesh component, used to calculate the movement delta between frames.
	FTransform SkelMeshComponentTransformWS = FTransform::Identity;

	// This is temporary to support compatibility with existing data until we switch Motion Matching to use FPoseSearchQueryTrajectory.
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory")
	FTrajectorySampleRange Temp_TrajectorySampleRange;
};