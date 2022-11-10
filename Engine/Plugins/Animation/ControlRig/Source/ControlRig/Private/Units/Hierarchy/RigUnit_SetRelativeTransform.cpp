// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetRelativeTransform.h"
#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Hierarchy/RigUnit_GetTransform.h"
#include "Units/Hierarchy/RigUnit_SetTransform.h"
#include "Units/Hierarchy/RigUnit_GetRelativeTransform.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_SetRelativeTransform)

FRigUnit_SetRelativeTransformForItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	FTransform ParentTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(ExecuteContext, Parent, EBoneGetterSetterMode::GlobalSpace, bParentInitial, ParentTransform, CachedParent, Context);
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(ExecuteContext, Value, ParentTransform, GlobalTransform, Context);
	FRigUnit_SetTransform::StaticExecute(ExecuteContext, Child, EBoneGetterSetterMode::GlobalSpace, false, GlobalTransform, Weight, bPropagateToChildren, CachedChild, Context);
}

FRigUnit_SetRelativeTranslationForItem_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	FTransform ParentTransform = FTransform::Identity;
	FTransform LocalTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(ExecuteContext, Parent, EBoneGetterSetterMode::GlobalSpace, bParentInitial, ParentTransform, CachedParent, Context);
	FRigUnit_GetRelativeTransformForItem::StaticExecute(ExecuteContext, Child, false, Parent, bParentInitial, LocalTransform, CachedChild, CachedParent, Context);
	LocalTransform.SetTranslation(Value);
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(ExecuteContext, LocalTransform, ParentTransform, GlobalTransform, Context);
	FRigUnit_SetTransform::StaticExecute(ExecuteContext, Child, EBoneGetterSetterMode::GlobalSpace, false, GlobalTransform, Weight, bPropagateToChildren, CachedChild, Context);
}

FRigUnit_SetRelativeRotationForItem_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	FTransform ParentTransform = FTransform::Identity;
	FTransform LocalTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(ExecuteContext, Parent, EBoneGetterSetterMode::GlobalSpace, bParentInitial, ParentTransform, CachedParent, Context);
	FRigUnit_GetRelativeTransformForItem::StaticExecute(ExecuteContext, Child, false, Parent, bParentInitial, LocalTransform, CachedChild, CachedParent, Context);
	LocalTransform.SetRotation(Value);
	LocalTransform.NormalizeRotation();
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(ExecuteContext, LocalTransform, ParentTransform, GlobalTransform, Context);
	FRigUnit_SetTransform::StaticExecute(ExecuteContext, Child, EBoneGetterSetterMode::GlobalSpace, false, GlobalTransform, Weight, bPropagateToChildren, CachedChild, Context);
}

