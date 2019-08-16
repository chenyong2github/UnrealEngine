// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetRelativeBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetRelativeBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Relative Transform %s"), *Bone.ToString());
}

void FRigUnit_SetRelativeBoneTransform::Execute(const FRigUnitContext& Context)
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
				CachedSpaceIndex = Hierarchy->GetIndex(Space);
				// fall through to update
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE && CachedSpaceIndex != INDEX_NONE)
				{
					const FTransform SpaceTransform = Hierarchy->GetGlobalTransform(CachedSpaceIndex);
					const FTransform Absolute = Transform * SpaceTransform;
					Hierarchy->SetGlobalTransform(CachedBoneIndex, Absolute, bPropagateToChildren);
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_SetRelativeBoneTransform)
{
	Hierarchy.AddBone(TEXT("Root"), NAME_None, FTransform(FVector(1.f, 0.f, 0.f)));
	Hierarchy.AddBone(TEXT("BoneA"), TEXT("Root"), FTransform(FVector(1.f, 2.f, 3.f)));
	Hierarchy.AddBone(TEXT("BoneB"), TEXT("BoneA"), FTransform(FVector(1.f, 5.f, 3.f)));
	Hierarchy.AddBone(TEXT("BoneC"), TEXT("Root"), FTransform(FVector(-4.f, 0.f, 0.f)));
	Hierarchy.Initialize();
	Unit.ExecuteContext.HierarchyReference = HierarchyRef;

	Hierarchy.ResetTransforms();
	Unit.Bone = TEXT("BoneA");
	Unit.Space = TEXT("Root");
	Unit.Transform = FTransform(FVector(0.f, 0.f, 7.f));
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	Hierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 3.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	Hierarchy.ResetTransforms();
	Unit.Space = TEXT("BoneC");
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(-4.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	Hierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(-4.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(-4.f, 3.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(Hierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	return true;
}
#endif