// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Transform %s"), *Bone.ToString());
}

FRigUnit_SetBoneTransform_Execute()
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
				// fall through to update
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetGlobalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								const FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedBoneIndex);
								Hierarchy->SetGlobalTransform(CachedBoneIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, T), bPropagateToChildren);
							}
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetLocalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								const FTransform PreviousTransform = Hierarchy->GetLocalTransform(CachedBoneIndex);
								Hierarchy->SetLocalTransform(CachedBoneIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, T), bPropagateToChildren);
							}
							break;
						}
						default:
						{
							break;
						}
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_SetBoneTransform)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("BoneA"), ERigBoneType::User, FTransform(FVector(1.f, 5.f, 3.f)));
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;

	BoneHierarchy.ResetTransforms();
	Unit.Bone = TEXT("Root");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	Unit.Transform = FTransform(FVector(0.f, 0.f, 7.f));
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(0.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 2.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(0.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 2.f, 3.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(0.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(0.f, 2.f, 10.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(0.f, 5.f, 10.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Bone = TEXT("BoneA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(0.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(1.f, 5.f, 3.f)), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(1.f, 0.f, 7.f)), TEXT("unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(2).GetTranslation().Equals(FVector(1.f, 3.f, 7.f)), TEXT("unexpected transform"));

	return true;
}
#endif