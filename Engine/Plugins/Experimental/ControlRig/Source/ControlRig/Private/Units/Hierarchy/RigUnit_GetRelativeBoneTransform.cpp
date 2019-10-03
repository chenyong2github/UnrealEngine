// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetRelativeBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetRelativeBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Relative Transform %s"), *Bone.ToString());
}

FRigUnit_GetRelativeBoneTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigBoneHierarchy* Hierarchy = Context.GetBones();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBoneIndex = Hierarchy->GetIndex(Bone);
				CachedSpaceIndex = Hierarchy->GetIndex(Space);
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE && CachedSpaceIndex != INDEX_NONE)
				{
					const FTransform SpaceTransform = Hierarchy->GetGlobalTransform(CachedSpaceIndex);
					const FTransform BoneTransform = Hierarchy->GetGlobalTransform(CachedBoneIndex);
					Transform = BoneTransform.GetRelativeTransform(SpaceTransform);
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetRelativeBoneTransform)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(-4.f, 0.f, 0.f)));
	BoneHierarchy.Initialize();

	Unit.Bone = TEXT("Unknown");
	Unit.Space = TEXT("Root");
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected transform"));

	Unit.Bone = TEXT("BoneA");
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 2.f, 3.f)), TEXT("unexpected transform"));

	Unit.Space = TEXT("BoneB");
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(5.f, 2.f, 3.f)), TEXT("unexpected transform"));

	return true;
}
#endif