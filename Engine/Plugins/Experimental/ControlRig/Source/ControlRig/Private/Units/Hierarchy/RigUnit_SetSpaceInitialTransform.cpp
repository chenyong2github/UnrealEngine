// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceInitialTransform.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

FRigUnit_SetSpaceInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedSpaceIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				const FRigElementKey SpaceKey(SpaceName, ERigElementType::Null);
				if (!CachedSpaceIndex.UpdateCache(SpaceKey, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Space '%s' is not valid."), *SpaceName.ToString());
					return;
				}

				FTransform InitialTransform = Transform;
				if (Space == EBoneGetterSetterMode::GlobalSpace)
				{
					const FTransform ParentTransform = Hierarchy->GetParentTransformByIndex(CachedSpaceIndex, true);
					InitialTransform = InitialTransform.GetRelativeTransform(ParentTransform);
				}

				Hierarchy->SetInitialLocalTransform(CachedSpaceIndex, InitialTransform);
			}
			default:
			{
				break;
			}
		}
	}
}
