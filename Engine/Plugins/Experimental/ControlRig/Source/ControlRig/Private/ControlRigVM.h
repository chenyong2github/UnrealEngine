// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigDefines.h"

struct FRigUnitContext;

namespace ControlRigVM
{
	void Execute(UObject* OuterObject, const FRigUnitContext& Context, const TArray<FControlRigOperator>& InOperators, const ERigExecutionType ExecutionType);

	// execution one op
	bool ExecOp(UObject* OuterObject, const FRigUnitContext& Context, const ERigExecutionType ExecutionType, const FControlRigOperator& InOperator);
}