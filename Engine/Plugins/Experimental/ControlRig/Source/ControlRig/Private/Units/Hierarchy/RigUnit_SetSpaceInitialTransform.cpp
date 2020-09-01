// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceInitialTransform.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

FRigUnit_SetSpaceInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FRigSpaceHierarchy* Hierarchy = ExecuteContext.GetSpaces();
	if (Hierarchy)
	{
		FRigSpaceHierarchy& HierarchyRef = *Hierarchy;

		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedSpaceIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (!CachedSpaceIndex.UpdateCache(SpaceName, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Space '%s' is not valid."), *SpaceName.ToString());
					return;
				}

				FTransform InitialTransform = Transform;
				if (Space == EBoneGetterSetterMode::GlobalSpace)
				{
					FRigSpace& SpaceRef = HierarchyRef[CachedSpaceIndex];
					FTransform ParentTransform = ExecuteContext.Hierarchy->GetInitialGlobalTransform(SpaceRef.GetParentElementKey());
					InitialTransform = InitialTransform.GetRelativeTransform(ParentTransform);
				}

				HierarchyRef.SetInitialTransform(CachedSpaceIndex, InitialTransform);
			}
			default:
			{
				break;
			}
		}
	}
}
