// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetJointTransform.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"

FRigUnit_GetJointTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy)
	{
		int32 Index = Hierarchy->GetIndex(Joint);
		if (Index != INDEX_NONE)
		{
			FTransform GlobalTransform;
			switch (Type)
			{
			case ETransformGetterType::Current:
				{
					FTransform ComputedBaseTransform = UtilityHelpers::GetBaseTransformByMode(TransformSpace, [Hierarchy](const FRigElementKey& JointKey) { return Hierarchy->GetGlobalTransform(JointKey.Name); },
					(*Hierarchy)[Index].GetParentElementKey(), FRigElementKey(BaseJoint, ERigElementType::Bone), BaseTransform);

					Output = Hierarchy->GetInitialGlobalTransform(Index).GetRelativeTransform(ComputedBaseTransform);
					break;
				}
			case ETransformGetterType::Initial:
			default:
				{
					FTransform ComputedBaseTransform = UtilityHelpers::GetBaseTransformByMode(TransformSpace, [Hierarchy](const FRigElementKey& JointKey) { return Hierarchy->GetInitialGlobalTransform(JointKey.Name); },
					(*Hierarchy)[Index].GetParentElementKey(), FRigElementKey(BaseJoint, ERigElementType::Bone), BaseTransform);

					Output = Hierarchy->GetInitialGlobalTransform(Index).GetRelativeTransform(ComputedBaseTransform);
					break;
				}
			}
		}
	}
}
