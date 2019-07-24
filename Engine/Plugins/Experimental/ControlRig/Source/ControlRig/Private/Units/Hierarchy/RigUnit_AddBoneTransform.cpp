// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AddBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_AddBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Offset Transform %s"), *Bone.ToString());
}

void FRigUnit_AddBoneTransform::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyRef& HierarchyRef = ExecuteContext.HierarchyReference;
	FRigHierarchy* Hierarchy = HierarchyRef.Get();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBoneIndex = Hierarchy->GetIndex(Bone);
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE)
				{
					FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedBoneIndex);
					if (bPostMultiply)
					{
						Hierarchy->SetGlobalTransform(CachedBoneIndex, PreviousTransform * Transform, bPropagateToChildren);
					}
					else
					{
						Hierarchy->SetGlobalTransform(CachedBoneIndex, Transform * PreviousTransform, bPropagateToChildren);
					}
				}
			}
			default:
			{
				break;
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AddBoneTransform)
{
	Hierarchy.AddBone(TEXT("Root"), NAME_None, FTransform(FVector(1.f, 0.f, 0.f)));
	Hierarchy.AddBone(TEXT("BoneA"), TEXT("Root"), FTransform(FVector(1.f, 2.f, 0.f)));
	Hierarchy.Initialize();
	Unit.ExecuteContext.HierarchyReference = HierarchyRef;

	Hierarchy.ResetTransforms();
	Unit.Bone = TEXT("BoneA");
	Unit.Transform = FTransform(FVector(0.f, 0.f, 7.f));
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 2.f, 7.f)), TEXT("unexpected transform"));

	Unit.Bone = TEXT("Root");
	Hierarchy.ResetTransforms();
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 2.f, 0.f)), TEXT("unexpected transform"));

	Unit.bPropagateToChildren = true;
	Hierarchy.ResetTransforms();
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 2.f, 7.f)), TEXT("unexpected transform"));

	return true;
}
#endif