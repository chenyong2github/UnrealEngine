// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigVM.h"
#include "Units/RigUnit.h"
#include "ControlRigDefines.h"
#include "Units/RigUnitContext.h"

namespace ControlRigVM
{
	void Execute(UObject* OuterObject, const FRigUnitContext& Context, const TArray<FControlRigOperator>& InOperators, const ERigExecutionType ExecutionType)
	{
		const int32 TotalExec = InOperators.Num();
		for(const FControlRigOperator& Op : InOperators)
		{
			if (!ExecOp(OuterObject, Context, ExecutionType, Op))
			{
				// @todo: print warning?
				break;
			}
		}
	}

	bool ExecOp(UObject* OuterObject, const FRigUnitContext& Context, const ERigExecutionType ExecutionType, const FControlRigOperator& InOperator)
	{
		check(OuterObject);
		switch (InOperator.OpCode)
		{
		case EControlRigOpCode::Copy:
		{
			PropertyPathHelpers::CopyPropertyValueFast(OuterObject, InOperator.CachedPropertyPath2, InOperator.CachedPropertyPath1);
			return true;
		}
		case EControlRigOpCode::Exec:
		{
			FRigUnit* RigUnit = static_cast<FRigUnit*>(InOperator.CachedPropertyPath1.GetCachedAddress());
			bool bShouldExecute = (RigUnit->ExecutionType != EUnitExecutionType::Disable);
			if (bShouldExecute)
			{
				if (RigUnit->ExecutionType == EUnitExecutionType::Initialize)
				{
					bShouldExecute = Context.State == EControlRigState::Init;
				}
				else
				{
					bShouldExecute = (RigUnit->ExecutionType == EUnitExecutionType::Always) || (ExecutionType == ERigExecutionType::Editing);
				}
			}
			if (bShouldExecute)
			{
				RigUnit->Execute(Context);
			}
		}
		return true;
		case EControlRigOpCode::Done: // @do I need it?
			return false;
		}

		// invalid op code
		return false;
	}
}
