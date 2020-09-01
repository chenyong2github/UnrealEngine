// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlOffset.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

FRigUnit_SetControlOffset_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
					return;
				}

				FTransform OffsetToSet = Offset;
				if (Space == EBoneGetterSetterMode::GlobalSpace)
				{
					OffsetToSet = OffsetToSet.GetRelativeTransform(Hierarchy->GetParentInitialTransform(CachedControlIndex, false));
				}
				Hierarchy->SetControlOffset(CachedControlIndex, OffsetToSet);
			}
			default:
			{
				break;
			}
		}
	}
}
