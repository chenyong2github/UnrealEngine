// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneInitialTransform.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

FRigUnit_SetBoneInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy)
	{
		FRigBoneHierarchy& HierarchyRef = *Hierarchy;

		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBone.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (!CachedBone.UpdateCache(Bone, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone '%s' is not valid."), *Bone.ToString());
					return;
				}

				if (Space == EBoneGetterSetterMode::LocalSpace)
				{
					HierarchyRef.SetInitialLocalTransform(CachedBone, Transform);
				}
				else
				{
					HierarchyRef.SetInitialGlobalTransform(CachedBone, Transform);
				}
			}
			default:
			{
				break;
			}
		}
	}
}
