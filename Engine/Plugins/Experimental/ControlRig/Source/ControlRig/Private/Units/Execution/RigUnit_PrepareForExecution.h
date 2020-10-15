// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h"
#include "RigUnit_PrepareForExecution.generated.h"

/**
 * Event to setup elements before all solves
 */
USTRUCT(meta=(DisplayName="Setup Event", Category="Setup", TitleColor="1 0 0", NodeColor="0.1 0.1 0.1", Keywords="Setup,Init,Fit"))
struct CONTROLRIG_API FRigUnit_PrepareForExecution : public FRigUnit
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	virtual FName GetEventName() const override { return EventName; }

	// The execution result
	UPROPERTY(EditAnywhere, Transient, DisplayName = "Execute", Category = "PrepareForExecution", meta = (Output))
	FControlRigExecuteContext ExecuteContext;

	static FName EventName;
};
