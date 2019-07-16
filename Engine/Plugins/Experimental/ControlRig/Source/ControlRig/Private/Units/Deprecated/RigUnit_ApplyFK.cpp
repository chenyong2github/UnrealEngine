// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ApplyFK.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"

void FRigUnit_ApplyFK::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyRef& HierarchyRef = ExecuteContext.HierarchyReference;
	
	if (Context.State == EControlRigState::Init)
	{
		FRigHierarchy* Hierarchy = HierarchyRef.Get();
		if (!Hierarchy)
		{
			UnitLogHelpers::PrintMissingHierarchy(RigUnitName);
		}
	}
	else if (Context.State == EControlRigState::Update)
	{
		FRigHierarchy* Hierarchy = HierarchyRef.Get();
		if (Hierarchy)
		{
			int32 Index = Hierarchy->GetIndex(Joint);
			if (Index != INDEX_NONE)
			{
				// first filter input transform
				FTransform InputTransform = Transform;
				Filter.FilterTransform(InputTransform);

				// now get override or additive
				// whether I'd like to apply whole thing or not
				if (ApplyTransformMode == EApplyTransformMode::Override)
				{
					// get base transform
					FTransform InputBaseTransform = GetBaseTransform(Index, Hierarchy);
					FTransform ApplyTransform = InputTransform * InputBaseTransform;
					Hierarchy->SetGlobalTransform(Index, ApplyTransform);
				}
				else
				{
					// if additive, we get current transform and calculate base transform and apply in their local space
					FTransform CurrentTransform = Hierarchy->GetGlobalTransform(Index);
					FTransform InputBaseTransform = GetBaseTransform(Index, Hierarchy);
					FTransform LocalTransform = InputTransform * CurrentTransform.GetRelativeTransform(InputBaseTransform);
					// apply additive
					Hierarchy->SetGlobalTransform(Index, LocalTransform * InputBaseTransform);
				}
			}
		}
	}
}

FTransform FRigUnit_ApplyFK::GetBaseTransform(int32 BoneIndex, const FRigHierarchy* CurrentHierarchy) const
{
	return UtilityHelpers::GetBaseTransformByMode(ApplyTransformSpace, [CurrentHierarchy](const FName& BoneName) { return CurrentHierarchy->GetGlobalTransform(BoneName); },
		CurrentHierarchy->GetBones()[BoneIndex].ParentName, BaseJoint, BaseTransform);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_ApplyFK)
{
	Hierarchy.AddBone(TEXT("Root"), NAME_None, FTransform(FVector(1.f, 0.f, 0.f)));
	Hierarchy.AddBone(TEXT("BoneA"), TEXT("Root"), FTransform(FVector(1.f, 2.f, 3.f)));

	Unit.ExecuteContext = ExecuteContext;
	Unit.Joint = TEXT("BoneA");
	Unit.ApplyTransformMode = EApplyTransformMode::Override;
	Unit.ApplyTransformSpace = ETransformSpaceMode::GlobalSpace;
	Unit.Transform = FTransform(FVector(0.f, 5.f, 0.f));

	Hierarchy.Initialize();
	Execute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(0.f, 5.f, 0.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(Hierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(-1.f, 5.f, 0.f)), TEXT("unexpected local transform"));

	Unit.ApplyTransformMode = EApplyTransformMode::Override;
	Unit.ApplyTransformSpace = ETransformSpaceMode::LocalSpace;

	Hierarchy.Initialize();
	Execute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 5.f, 0.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(Hierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(0.f, 5.f, 0.f)), TEXT("unexpected local transform"));

	Unit.ApplyTransformMode = EApplyTransformMode::Additive;

	Hierarchy.Initialize();
	Execute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 7.f, 3.f)), TEXT("unexpected global transform"));
	AddErrorIfFalse(Hierarchy.GetLocalTransform(1).GetTranslation().Equals(FVector(0.f, 7.f, 3.f)), TEXT("unexpected local transform"));
	return true;
}
#endif