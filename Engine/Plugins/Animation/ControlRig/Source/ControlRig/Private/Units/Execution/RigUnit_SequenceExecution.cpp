// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SequenceExecution.h"
#include "Units/RigUnitContext.h"

FRigUnit_SequenceExecution_Execute()
{
	A = B = C = D = ExecuteContext;
}

FRigVMStructUpgradeInfo FRigUnit_SequenceExecution::GetUpgradeInfo() const
{
	FRigUnit_SequenceAggregate NewNode;
	
	FRigVMStructUpgradeInfo Info(*this, NewNode);

	// add two more pins
	Info.AddAggregatePin(); // "C"
	Info.AddAggregatePin(); // "D"

	return Info;
}

FRigUnit_SequenceAggregate_Execute()
{
	A = B = ExecuteContext;
}
