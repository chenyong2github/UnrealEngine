// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetInitialBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetInitialBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Initial Transform %s"), *Bone.ToString());
}

FRigUnit_GetInitialBoneTransform_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Transform = Hierarchy->GetInitialTransform(CachedBoneIndex);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Transform = Hierarchy->GetInitialTransform(CachedBoneIndex);
							int32 ParentBoneIndex = (*Hierarchy)[CachedBoneIndex].ParentIndex;
							if (ParentBoneIndex != INDEX_NONE)
							{
								FTransform ParentTransform = Hierarchy->GetInitialTransform(ParentBoneIndex);
								Transform.SetToRelativeTransform(ParentTransform);
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetInitialBoneTransform)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Initialize();

	Unit.Bone = TEXT("Root");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Bone = TEXT("BoneA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 2.f, 3.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 2.f, 3.f)), TEXT("unexpected local transform"));

	return true;
}
#endif