// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Simulation/RigUnit_SimBase.h"
#include "RigUnit_Timeline.generated.h"

/**
 * Simulates a time value - can act as a timeline playing back
 */
USTRUCT(meta=(DisplayName="Accumulated Time", Keywords="Playback,Pause,Timeline"))
struct FRigUnit_Timeline : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_Timeline()
	{
		Speed = 1.f;
		Time = AccumulatedValue = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	float Speed;

	UPROPERTY(meta=(Output))
	float Time;

	UPROPERTY()
	float AccumulatedValue;
};
