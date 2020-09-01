// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_OffsetTransform.h"
#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Hierarchy/RigUnit_GetTransform.h"
#include "Units/Hierarchy/RigUnit_SetTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_OffsetTransformForItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	FTransform PreviousTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(RigVMExecuteContext, Item, EBoneGetterSetterMode::GlobalSpace, false, PreviousTransform, CachedIndex, Context);
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(RigVMExecuteContext, OffsetTransform, PreviousTransform, GlobalTransform, Context);
	FRigUnit_SetTransform::StaticExecute(RigVMExecuteContext, Item, EBoneGetterSetterMode::GlobalSpace, false, GlobalTransform, Weight, bPropagateToChildren, CachedIndex, ExecuteContext, Context);
}