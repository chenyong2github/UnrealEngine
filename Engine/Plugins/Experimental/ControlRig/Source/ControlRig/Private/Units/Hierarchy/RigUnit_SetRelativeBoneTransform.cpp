// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetRelativeBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetRelativeBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Relative Transform %s"), *Bone.ToString());
}

FRigUnit_SetRelativeBoneTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
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
					FTransform TargetTransform = Transform * SpaceTransform;

					if (!FMath::IsNearlyEqual(Weight, 1.f))
					{
						float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
						const FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedBoneIndex);
						TargetTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, TargetTransform, T);
					}

					Hierarchy->SetGlobalTransform(CachedBoneIndex, TargetTransform, bPropagateToChildren);
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
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("BoneA"), ERigBoneType::User, FTransform(FVector(1.f, 5.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneC"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(-4.f, 0.f, 0.f)));
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;

	BoneHierarchy.ResetTransforms();
	Unit.Bone = TEXT("BoneA");
	Unit.Space = TEXT("Root");
	Unit.Transform = FTransform(FVector(0.f, 0.f, 7.f));
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 3.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Space = TEXT("BoneC");
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(-4.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(-4.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(3).GetTranslation().Equals(FVector(-4.f, 3.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(-4.f, 0.f, 0.f)), TEXT("unexpected transform"));

	return true;
}
#endif