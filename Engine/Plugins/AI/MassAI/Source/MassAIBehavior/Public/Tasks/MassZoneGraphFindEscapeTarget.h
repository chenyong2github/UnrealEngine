// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassZoneGraphFindEscapeTarget.generated.h"

struct FStateTreeExecutionContext;
struct FMassZoneGraphLaneLocationFragment;
class UZoneGraphSubsystem;
class UZoneGraphAnnotationSubsystem;

/**
 * Updates TargetLocation to a escape target based on the agents current location on ZoneGraph, and disturbance annotation.
 */
USTRUCT()
struct MASSAIBEHAVIOR_API FMassZoneGraphFindEscapeTargetInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = Output)
	FMassZoneGraphTargetLocation EscapeTargetLocation;
};

USTRUCT(meta = (DisplayName = "ZG Find Escape Target"))
struct MASSAIBEHAVIOR_API FMassZoneGraphFindEscapeTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassZoneGraphFindEscapeTarget();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FMassZoneGraphFindEscapeTargetInstanceData::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;

	TStateTreeExternalDataHandle<FMassZoneGraphLaneLocationFragment> LocationHandle;
	TStateTreeExternalDataHandle<UZoneGraphSubsystem> ZoneGraphSubsystemHandle;
	TStateTreeExternalDataHandle<UZoneGraphAnnotationSubsystem> ZoneGraphAnnotationSubsystemHandle;

	TStateTreeInstanceDataPropertyHandle<FMassZoneGraphTargetLocation> EscapeTargetLocationHandle;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FZoneGraphTag DisturbanceAnnotationTag = FZoneGraphTag::None;
};
