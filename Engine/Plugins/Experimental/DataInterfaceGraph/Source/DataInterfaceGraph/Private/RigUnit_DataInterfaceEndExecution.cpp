// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_DataInterfaceEndExecution.h"
#include "Units/RigUnitContext.h"

FRigUnit_DataInterfaceEndExecution_Bool_Execute()
{
	if(Context.State == EControlRigState::Update)
	{
		SetResult(ExecuteContext, Result);
	}
}

FRigUnit_DataInterfaceEndExecution_Float_Execute()
{
	if(Context.State == EControlRigState::Update)
	{
		SetResult(ExecuteContext, Result);
	}
}
