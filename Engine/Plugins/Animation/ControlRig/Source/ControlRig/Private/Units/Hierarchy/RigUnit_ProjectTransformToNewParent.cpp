// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ProjectTransformToNewParent.h"
#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Hierarchy/RigUnit_GetTransform.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_ProjectTransformToNewParent)

FRigUnit_ProjectTransformToNewParent_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FTransform ChildTransform = FTransform::Identity;
	FTransform OldParentTransform = FTransform::Identity;
	FTransform NewParentTransform = FTransform::Identity;
	FTransform RelativeTransform = FTransform::Identity;

	FRigUnit_GetTransform::StaticExecute(ExecuteContext, Child, EBoneGetterSetterMode::GlobalSpace, bChildInitial, ChildTransform, CachedChild);
	FRigUnit_GetTransform::StaticExecute(ExecuteContext, OldParent, EBoneGetterSetterMode::GlobalSpace, bOldParentInitial, OldParentTransform, CachedOldParent);
	FRigUnit_GetTransform::StaticExecute(ExecuteContext, NewParent, EBoneGetterSetterMode::GlobalSpace, bNewParentInitial, NewParentTransform, CachedNewParent);
	FRigUnit_MathTransformMakeRelative::StaticExecute(ExecuteContext, ChildTransform, OldParentTransform, RelativeTransform);
	FRigUnit_MathTransformMakeAbsolute::StaticExecute(ExecuteContext, RelativeTransform, NewParentTransform, Transform);
}

