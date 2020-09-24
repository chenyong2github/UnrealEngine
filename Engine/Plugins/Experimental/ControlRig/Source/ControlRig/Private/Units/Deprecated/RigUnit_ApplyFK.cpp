// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ApplyFK.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"

FRigUnit_ApplyFK_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	
	if (Context.State == EControlRigState::Init)
	{
		return;
	}
	else if (Context.State == EControlRigState::Update)
	{
		FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
		if (Hierarchy)
		{
			int32 Index = Hierarchy->GetIndex(Joint);
			if (Index != INDEX_NONE)
			{
				// first filter input transform
				FTransform InputTransform = Transform;
				Filter.FilterTransform(InputTransform);

				FTransform InputBaseTransform = UtilityHelpers::GetBaseTransformByMode(
					ApplyTransformSpace,
					[Hierarchy](const FRigElementKey& BoneKey) { return Hierarchy->GetGlobalTransform(BoneKey.Name); },
					(*Hierarchy)[Index].GetParentElementKey(),
					FRigElementKey(BaseJoint, ERigElementType::Bone),
					BaseTransform
				);

				// now get override or additive
				// whether I'd like to apply whole thing or not
				if (ApplyTransformMode == EApplyTransformMode::Override)
				{
					// get base transform
					FTransform ApplyTransform = InputTransform * InputBaseTransform;
					Hierarchy->SetGlobalTransform(Index, ApplyTransform);
				}
				else
				{
					// if additive, we get current transform and calculate base transform and apply in their local space
					FTransform CurrentTransform = Hierarchy->GetGlobalTransform(Index);
					FTransform LocalTransform = InputTransform * CurrentTransform.GetRelativeTransform(InputBaseTransform);
					// apply additive
					Hierarchy->SetGlobalTransform(Index, LocalTransform * InputBaseTransform);
				}
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_ApplyFK)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));

	Unit.ExecuteContext = ExecuteContext;
	Unit.Joint = TEXT("BoneA");
	Unit.ApplyTransformMode = EApplyTransformMode::Override;
	Unit.ApplyTransformSpace = ETransformSpaceMode::GlobalSpace;
	Unit.Transform = FTransform(FVector(0.f, 5.f, 0.f));

	BoneHierarchy.Initialize();
	Execute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(0.f, 5.f, 0.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(BoneHierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(-1.f, 5.f, 0.f)), TEXT("unexpected local transform"));

	Unit.ApplyTransformMode = EApplyTransformMode::Override;
	Unit.ApplyTransformSpace = ETransformSpaceMode::LocalSpace;

	BoneHierarchy.Initialize();
	Execute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 5.f, 0.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(BoneHierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(0.f, 5.f, 0.f)), TEXT("unexpected local transform"));

	Unit.ApplyTransformMode = EApplyTransformMode::Additive;

	BoneHierarchy.Initialize();
	Execute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 7.f, 3.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(BoneHierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(0.f, 7.f, 3.f)), TEXT("unexpected local transform"));
	return true;
}
#endif