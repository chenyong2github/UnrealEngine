// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_TwoBoneIKSimple.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"
#include "TwoBoneIK.h"

FRigUnit_TwoBoneIKSimple_Execute()
{
	FRigUnit_TwoBoneIKSimplePerItem::StaticExecute(
		RigVMExecuteContext, 
		FRigElementKey(BoneA, ERigElementType::Bone),
		FRigElementKey(BoneB, ERigElementType::Bone),
		FRigElementKey(EffectorBone, ERigElementType::Bone),
		Effector,
		PrimaryAxis,
		SecondaryAxis,
		SecondaryAxisWeight,
		PoleVector,
		PoleVectorKind,
		FRigElementKey(PoleVectorSpace, ERigElementType::Bone),
		bEnableStretch,
		StretchStartRatio,
		StretchMaximumRatio,
		Weight,
		BoneALength,
		BoneBLength,
		bPropagateToChildren,
		DebugSettings,
		CachedBoneAIndex,
		CachedBoneBIndex,
		CachedEffectorBoneIndex,
		CachedPoleVectorSpaceIndex,
		ExecuteContext, 
		Context);
}

FRigUnit_TwoBoneIKSimplePerItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		CachedItemAIndex.Reset();
		CachedItemBIndex.Reset();
		CachedEffectorItemIndex.Reset();
		CachedPoleVectorSpaceIndex.Reset();
		return;
	}

	if (!CachedItemAIndex.UpdateCache(ItemA, Hierarchy) ||
		!CachedItemBIndex.UpdateCache(ItemB, Hierarchy) ||
		!CachedEffectorItemIndex.UpdateCache(EffectorItem, Hierarchy))
	{
		return;
	}

	CachedPoleVectorSpaceIndex.UpdateCache(PoleVectorSpace, Hierarchy);

	if (Weight <= SMALL_NUMBER)
	{
		return;
	}

	FVector PoleTarget = PoleVector;
	if (CachedPoleVectorSpaceIndex.IsValid())
	{
		const FTransform PoleVectorSpaceTransform = Hierarchy->GetGlobalTransform(CachedPoleVectorSpaceIndex);
		if (PoleVectorKind == EControlRigVectorKind::Direction)
		{
			PoleTarget = PoleVectorSpaceTransform.TransformVectorNoScale(PoleTarget);
		}
		else
		{
			PoleTarget = PoleVectorSpaceTransform.TransformPositionNoScale(PoleTarget);
		}
	}

	FTransform TransformA = Hierarchy->GetGlobalTransform(CachedItemAIndex);
	FTransform TransformB = TransformA;
	TransformB.SetLocation(Hierarchy->GetGlobalTransform(CachedItemBIndex).GetLocation());
	FTransform TransformC = Effector;

	float LengthA = ItemALength;
	float LengthB = ItemBLength;

	if (LengthA < SMALL_NUMBER)
	{
		FVector Diff = Hierarchy->GetInitialGlobalTransform(CachedItemAIndex).GetLocation() - Hierarchy->GetInitialGlobalTransform(CachedItemBIndex).GetLocation();
		Diff = Diff * TransformA.GetScale3D();
		LengthA = Diff.Size();
	}

	if (LengthB < SMALL_NUMBER && CachedEffectorItemIndex != INDEX_NONE)
	{
		FVector Diff = Hierarchy->GetInitialGlobalTransform(CachedItemBIndex).GetLocation() - Hierarchy->GetInitialGlobalTransform(CachedEffectorItemIndex).GetLocation();
		Diff = Diff * TransformB.GetScale3D();
		LengthB = Diff.Size();
	}

	if (LengthA < SMALL_NUMBER || LengthB < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Item Lengths are not provided.\nEither set item length(s) or set effector item."));
		return;
	}

	FControlRigMathLibrary::SolveBasicTwoBoneIK(TransformA, TransformB, TransformC, PoleTarget, PrimaryAxis, SecondaryAxis, SecondaryAxisWeight, LengthA, LengthB, bEnableStretch, StretchStartRatio, StretchMaximumRatio);

	if (Context.DrawInterface != nullptr && DebugSettings.bEnabled)
	{
		const FLinearColor Dark = FLinearColor(0.f, 0.2f, 1.f, 1.f);
		const FLinearColor Bright = FLinearColor(0.f, 1.f, 1.f, 1.f);
		Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, TransformA.GetLocation(), TransformB.GetLocation(), Dark);
		Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, TransformB.GetLocation(), TransformC.GetLocation(), Dark);
		Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, TransformB.GetLocation(), PoleTarget, Bright);
		Context.DrawInterface->DrawBox(DebugSettings.WorldOffset, FTransform(FQuat::Identity, PoleTarget, FVector(1.f, 1.f, 1.f) * DebugSettings.Scale * 0.1f), Bright);
	}

	if (Weight < 1.0f - SMALL_NUMBER)
	{
		FVector PositionB = TransformA.InverseTransformPosition(TransformB.GetLocation());
		FVector PositionC = TransformB.InverseTransformPosition(TransformC.GetLocation());
		TransformA.SetRotation(FQuat::Slerp(Hierarchy->GetGlobalTransform(CachedItemAIndex).GetRotation(), TransformA.GetRotation(), Weight));
		TransformB.SetRotation(FQuat::Slerp(Hierarchy->GetGlobalTransform(CachedItemBIndex).GetRotation(), TransformB.GetRotation(), Weight));
		TransformC.SetRotation(FQuat::Slerp(Hierarchy->GetGlobalTransform(CachedEffectorItemIndex).GetRotation(), TransformC.GetRotation(), Weight));
		TransformB.SetLocation(TransformA.TransformPosition(PositionB));
		TransformC.SetLocation(TransformB.TransformPosition(PositionC));
	}

	Hierarchy->SetGlobalTransform(CachedItemAIndex, TransformA, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(CachedItemBIndex, TransformB, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(CachedEffectorItemIndex, TransformC, bPropagateToChildren);
}

FRigUnit_TwoBoneIKSimpleVectors_Execute()
{
	AnimationCore::SolveTwoBoneIK(Root, Elbow, Effector, PoleVector, Effector, Elbow, Effector, BoneALength, BoneBLength, bEnableStretch, StretchStartRatio, StretchMaximumRatio);
}

FRigUnit_TwoBoneIKSimpleTransforms_Execute()
{
	FControlRigMathLibrary::SolveBasicTwoBoneIK(Root, Elbow, Effector, PoleVector, PrimaryAxis, SecondaryAxis, SecondaryAxisWeight, BoneALength, BoneBLength, bEnableStretch, StretchStartRatio, StretchMaximumRatio);
}