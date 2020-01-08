// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h"
#include "RigUnit_BeginExecution.generated.h"

USTRUCT(meta=(DisplayName="Begin Execute", Category="Execution", TitleColor="1 0 0", NodeColor="0.1 0.1 0.1"))
struct FRigUnit_BeginExecution : public FRigUnit
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	// The execution result
	UPROPERTY(EditAnywhere, Transient, DisplayName = "Execute", Category = "BeginExecution", meta = (Output))
	FControlRigExecuteContext ExecuteContext;
};
