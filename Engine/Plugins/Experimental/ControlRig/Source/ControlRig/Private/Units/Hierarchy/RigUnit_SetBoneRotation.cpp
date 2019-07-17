// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneRotation.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetBoneRotation::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Rotation %s"), *Bone.ToString());
}

void FRigUnit_SetBoneRotation::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyRef& HierarchyRef = ExecuteContext.HierarchyReference;
	FRigBoneHierarchy* Hierarchy = HierarchyRef.GetBones();
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
							FTransform Transform = Hierarchy->GetGlobalTransform(CachedBoneIndex);
							Transform.SetRotation(Rotation);
							Hierarchy->SetGlobalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							FTransform Transform = Hierarchy->GetLocalTransform(CachedBoneIndex);
							Transform.SetRotation(Rotation);
							Hierarchy->SetLocalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_SetBoneRotation)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, FTransform(FQuat(FVector(-1.f, 0.f, 0.f), 0.1f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), FTransform(FQuat(FVector(-1.f, 0.f, 0.f), 0.5f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("BoneA"), FTransform(FQuat(FVector(-1.f, 0.f, 0.f), 0.7f)));
	BoneHierarchy.Initialize();

	Unit.ExecuteContext.HierarchyReference = HierarchyRef;

	BoneHierarchy.ResetTransforms();
	Unit.Bone = TEXT("Root");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	Unit.Rotation = FQuat(FVector(-1.f, 0.f, 0.f), 0.25f);
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.25f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.5f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.7f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	BoneHierarchy.ResetTransforms();
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.25f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.5f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.7f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.25f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.65f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.85f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	BoneHierarchy.ResetTransforms();
	Unit.Bone = TEXT("BoneA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	Unit.bPropagateToChildren = false;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.1f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.25f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.7f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	BoneHierarchy.ResetTransforms();
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.1f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.35f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.7f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	BoneHierarchy.ResetTransforms();
	Unit.bPropagateToChildren = true;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle(), 0.1f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(0).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle(), 0.35f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(1).GetRotation().GetAngle()));
	AddErrorIfFalse(FMath::IsNearlyEqual(BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle(), 0.55f, 0.001f), FString::Printf(TEXT("unexpected angle %.04f"), BoneHierarchy.GetGlobalTransform(2).GetRotation().GetAngle()));

	return true;
}
#endif