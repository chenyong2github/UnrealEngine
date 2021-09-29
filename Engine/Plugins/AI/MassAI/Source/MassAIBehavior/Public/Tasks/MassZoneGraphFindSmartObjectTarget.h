// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "MassStateTreeTypes.h"
#include "MassZoneGraphPathFollowTask.h"

#include "MassZoneGraphFindSmartObjectTarget.generated.h"

/**
* Computes move target to a smart object based on current location on ZoneGraph.
*/
USTRUCT(meta = (DisplayName = "ZG Find Smart Object Target"))
struct MASSAIBEHAVIOR_API FMassZoneGraphFindSmartObjectTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="DataFragment_SmartObjectUser"))
	FStateTreeExternalItemHandle SmartObjectUserHandle;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphLaneLocationFragment"))
	FStateTreeExternalItemHandle LocationHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphAnnotationSubsystem"))
	FStateTreeExternalItemHandle AnnotationSubsystemHandle;

	FMassZoneGraphTargetLocation TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef TargetLocationRef;
};