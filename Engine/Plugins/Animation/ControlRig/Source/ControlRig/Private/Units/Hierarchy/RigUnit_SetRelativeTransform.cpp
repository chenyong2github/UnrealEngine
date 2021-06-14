// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetRelativeTransform.h"
#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Hierarchy/RigUnit_GetTransform.h"
#include "Units/Hierarchy/RigUnit_SetTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_SetRelativeTransformForItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	FTransform ParentTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(RigVMExecuteContext, Parent, EBoneGetterSetterMode::GlobalSpace, bParentInitial, ParentTransform, CachedParent, Context);
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(RigVMExecuteContext, RelativeTransform, ParentTransform, GlobalTransform, Context);
	FRigUnit_SetTransform::StaticExecute(RigVMExecuteContext, Child, EBoneGetterSetterMode::GlobalSpace, false, GlobalTransform, Weight, bPropagateToChildren, CachedChild, ExecuteContext, Context);
}