// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MotionTrajectory.h"

#include "MotionTrajectoryCharacterMovement.generated.h"

// Example motion trajectory component implementation for encapsulating: Character Movement ground locomotion
UCLASS(meta=(BlueprintSpawnableComponent), Category="Motion Trajectory")
class MOTIONTRAJECTORY_API UCharacterMovementTrajectoryComponent : public UMotionTrajectoryComponent
{
	GENERATED_BODY()

protected:

	// Begin UMotionTrajectoryComponent Interface
	virtual FTrajectorySample CalcWorldSpacePresentTrajectorySample(float DeltaTime) const override;
	virtual void TickTrajectory(float DeltaTime) override;
	// End UMotionTrajectoryComponent Interface

	UFUNCTION()
	void OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);

	UPROPERTY(Transient)
	FRotator LastDesiredControlRotation = FRotator::ZeroRotator;
	
	UPROPERTY(Transient)
	FRotator DesiredControlRotationVelocity = FRotator::ZeroRotator;

public:

	UCharacterMovementTrajectoryComponent(const FObjectInitializer& ObjectInitializer);

	// Begin UActorComponent Interface
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	// End UActorComponent Interface

	// Begin UMotionTrajectoryComponent Interface
	virtual FTrajectorySampleRange GetTrajectory() const override;
	virtual FTrajectorySampleRange GetTrajectoryWithSettings(const FMotionTrajectorySettings& Settings, bool bIncludeHistory) const override;
	// End UMotionTrajectoryComponent Interface
};