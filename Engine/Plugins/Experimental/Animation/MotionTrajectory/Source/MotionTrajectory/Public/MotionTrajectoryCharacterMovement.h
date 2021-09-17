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
	virtual FTrajectorySample GetPresentTrajectory() const override;
	virtual FTransform GetPresentWorldTransform() const override;
	// End UMotionTrajectoryComponent Interface

public:

	UCharacterMovementTrajectoryComponent(const FObjectInitializer& ObjectInitializer);

	// Begin UActorComponent Interface
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	// End UActorComponent Interface

	// Begin UMotionTrajectoryComponent Interface
	virtual FTrajectorySampleRange GetTrajectory() const override;
	virtual FTrajectorySampleRange GetTrajectoryWithSettings(const FMotionTrajectorySettings& Settings, bool bIncludeHistory) const override;
	// End UMotionTrajectoryComponent Interface
};