// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ModifyBoneTransforms.h"
#include "Units/RigUnitContext.h"

FRigUnit_ModifyBoneTransforms_Execute()
{
	TArray<FRigUnit_ModifyTransforms_PerItem> ItemsToModify;
	for (int32 BoneIndex = 0; BoneIndex < BoneToModify.Num(); BoneIndex++)
	{
		FRigUnit_ModifyTransforms_PerItem ItemToModify;
		ItemToModify.Item = FRigElementKey(BoneToModify[BoneIndex].Bone, ERigElementType::Bone);
		ItemToModify.Transform = BoneToModify[BoneIndex].Transform;
		ItemsToModify.Add(ItemToModify);
	}

	FRigUnit_ModifyTransforms::StaticExecute(
		RigVMExecuteContext,
		ItemsToModify,
		Weight,
		WeightMinimum,
		WeightMaximum,
		Mode,
		WorkData,
		ExecuteContext, 
		Context);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_ModifyBoneTransforms)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(5.f, 6.f, 7.f)));
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;

	Unit.BoneToModify.SetNumZeroed(2);
	Unit.BoneToModify[0].Bone = TEXT("BoneA");
	Unit.BoneToModify[1].Bone = TEXT("BoneB");
	Unit.BoneToModify[0].Transform = Unit.BoneToModify[1].Transform = FTransform(FVector(10.f, 11.f, 12.f));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveLocal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 13.f, 15.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(15.f, 17.f, 19.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveGlobal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 13.f, 15.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(15.f, 17.f, 19.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::OverrideLocal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(11.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::OverrideGlobal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(10.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(10.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveLocal;
	Unit.Weight = 0.5f;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(6.f, 7.5f, 9.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(10.f, 11.5f, 13.f)).IsNearlyZero(), TEXT("unexpected transform"));


	return true;
}
#endif