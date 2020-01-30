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
					FTransform ComputedBaseTransform = UtilityHelpers::GetBaseTransformByMode(TransformSpace, [Hierarchy](const FName& JointName) { return Hierarchy->GetGlobalTransform(JointName); },
					(*Hierarchy)[Index].ParentName, BaseJoint, BaseTransform);

					Output = Hierarchy->GetGlobalTransform(Index).GetRelativeTransform(ComputedBaseTransform);
					break;
				}
			case ETransformGetterType::Initial:
			default:
				{
				FTransform ComputedBaseTransform = UtilityHelpers::GetBaseTransformByMode(TransformSpace, [Hierarchy](const FName& JointName) { return Hierarchy->GetInitialTransform(JointName); },
					(*Hierarchy)[Index].ParentName, BaseJoint, BaseTransform);

					Output = Hierarchy->GetInitialTransform(Index).GetRelativeTransform(ComputedBaseTransform);
					break;
				}
			}
		}
	}
}
