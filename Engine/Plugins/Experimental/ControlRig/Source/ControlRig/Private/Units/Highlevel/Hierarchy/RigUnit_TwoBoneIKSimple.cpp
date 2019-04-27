// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_TwoBoneIKSimple.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

void FRigUnit_TwoBoneIKSimple::Execute(const FRigUnitContext& Context)
{
	FRigHierarchy* Hierarchy = (FRigHierarchy*)(Context.HierarchyReference.Get());
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		BoneAIndex = Hierarchy->GetIndex(BoneA);
		BoneBIndex = Hierarchy->GetIndex(BoneB);
		EffectorBoneIndex = Hierarchy->GetIndex(EffectorBone);
		PoleVectorSpaceIndex = Hierarchy->GetIndex(PoleVectorSpace);
		return;
	}

	if (BoneAIndex == INDEX_NONE || BoneBIndex == INDEX_NONE)
	{
		return;
	}

	if (Weight <= SMALL_NUMBER)
	{
		return;
	}

	float LengthA = BoneALength;
	float LengthB = BoneBLength;

	if (LengthA < SMALL_NUMBER)
	{
		LengthA = (Hierarchy->GetInitialTransform(BoneAIndex).GetLocation() - Hierarchy->GetInitialTransform(BoneBIndex).GetLocation()).Size();
	}

	if (LengthB < SMALL_NUMBER && EffectorBoneIndex != INDEX_NONE)
	{
		LengthB = (Hierarchy->GetInitialTransform(BoneBIndex).GetLocation() - Hierarchy->GetInitialTransform(EffectorBoneIndex).GetLocation()).Size();
	}

	if (LengthA < SMALL_NUMBER || LengthB < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone Lengths are not provided.\nEither set bone length(s) or set effector bone."));
		return;
	}

	FVector PoleTarget = PoleVector;
	if (PoleVectorSpaceIndex != INDEX_NONE)
	{
		const FTransform PoleVectorSpaceTransform = Hierarchy->GetGlobalTransform(PoleVectorSpaceIndex);
		if (PoleVectorKind == EControlRigVectorKind::Direction)
		{
			PoleTarget = PoleVectorSpaceTransform.TransformVectorNoScale(PoleTarget);
		}
		else
		{
			PoleTarget = PoleVectorSpaceTransform.TransformPositionNoScale(PoleTarget);
		}
	}

	FTransform TransformA = Hierarchy->GetGlobalTransform(BoneAIndex);
	FTransform TransformB = TransformA;
	TransformB.SetLocation(Hierarchy->GetGlobalTransform(BoneBIndex).GetLocation());
	FTransform TransformC = Effector;

	FControlRigMathLibrary::SolveBasicTwoBoneIK(TransformA, TransformB, TransformC, PoleTarget, PrimaryAxis, SecondaryAxis, LengthA, LengthB, bEnableStretch, StretchStartRatio, StretchMaximumRatio);

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
		TransformA = FControlRigMathLibrary::LerpTransform(Hierarchy->GetGlobalTransform(BoneAIndex), TransformA, Weight);
		TransformB = FControlRigMathLibrary::LerpTransform(Hierarchy->GetGlobalTransform(BoneBIndex), TransformB, Weight);
		TransformC = FControlRigMathLibrary::LerpTransform(Effector, TransformC, Weight);
	}

	Hierarchy->SetGlobalTransform(BoneAIndex, TransformA, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(BoneBIndex, TransformB, bPropagateToChildren);
	if (EffectorBoneIndex != INDEX_NONE)
	{
		Hierarchy->SetGlobalTransform(EffectorBoneIndex, TransformC, bPropagateToChildren);
	}
}