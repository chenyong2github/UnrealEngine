// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassStateTreeTypes.h"
#include "MassLookAtFragments.h"
#include "MassLookAtTask.generated.h"

/**
 * Task to assign a LookAt target for mass processing
 */
USTRUCT(meta = (DisplayName = "Mass LookAt Task"))
struct MASSAIBEHAVIOR_API FMassLookAtTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta = (BaseClass="MassSignalSubsystem"))
	FStateTreeExternalItemHandle MassSignalSubsystemHandle;

	UPROPERTY(meta = (BaseStruct="MassLookAtFragment"))
	FStateTreeExternalItemHandle LookAtHandle;

	/** Delay before the task ends. Default (0 or any negative) will run indefinitely so it requires a transition in the state tree to stop it. */
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (Bindable))
	float Duration = 0.f;

	/** Look At Mode */
	UPROPERTY(EditAnywhere, Category = Params, meta = (Bindable))
	EMassLookAtMode LookAtMode = EMassLookAtMode::LookForward; 

	/** Entity to set as the target for the LookAt behavior. */
	UPROPERTY(VisibleAnywhere, Category = Params, meta = (Bindable))
	FMassEntityHandle TargetEntity;

	/** Random gaze Mode */
	UPROPERTY(EditAnywhere, Category = Params, meta = (Bindable))
	EMassLookAtGazeMode RandomGazeMode = EMassLookAtGazeMode::None;
	
	/** Random gaze yaw angle added to the look direction determined by the look at mode. */
	UPROPERTY(EditAnywhere, Category = Params, meta = (Bindable, UIMin = 0.0, ClampMin = 0.0, UIMax = 180.0, ClampMax = 180.0))
	uint8 RandomGazeYawVariation = 0;

	/** Random gaze pitch angle added to the look direction determined by the look at mode. */
	UPROPERTY(EditAnywhere, Category = Params, meta = (Bindable, UIMin = 0.0, ClampMin = 0.0, UIMax = 180.0, ClampMax = 180.0))
	uint8 RandomGazePitchVariation = 0;

	/** If true, allow random gaze to look at other entities too. */
	UPROPERTY(EditAnywhere, Category = Params, meta = (Bindable))
	bool bRandomGazeEntities = false;

	/** Accumulated time used to stop task if duration is set */
	float Time = 0.f;
};
