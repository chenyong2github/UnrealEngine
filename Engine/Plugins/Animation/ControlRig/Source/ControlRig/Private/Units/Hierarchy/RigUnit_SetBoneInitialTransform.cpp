// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneInitialTransform.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

FRigUnit_SetBoneInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBone.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				const FRigElementKey Key(Bone, ERigElementType::Bone);
				if (!CachedBone.UpdateCache(Key, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone '%s' is not valid."), *Bone.ToString());
					return;
				}

				if (Space == EBoneGetterSetterMode::LocalSpace)
				{
					Hierarchy->SetInitialLocalTransform(CachedBone, Transform);
				}
				else
				{
					Hierarchy->SetInitialGlobalTransform(CachedBone, Transform);
				}
			}
			default:
			{
				break;
			}
		}
	}
}
