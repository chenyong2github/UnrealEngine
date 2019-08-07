// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_SlideChain.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"

FRigUnit_SlideChain_Execute()
{

	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	float& ChainLength = WorkData.ChainLength;
	TArray<float>& BoneSegments = WorkData.BoneSegments;
	TArray<int32>& BoneIndices = WorkData.BoneIndices;
	TArray<FTransform>& Transforms = WorkData.Transforms;
	TArray<FTransform>& BlendedTransforms = WorkData.BlendedTransforms;

	if (Context.State == EControlRigState::Init)
	{
		BoneSegments.Reset();
		BoneIndices.Reset();
		Transforms.Reset();
		BlendedTransforms.Reset();

		ChainLength = 0.f;

		int32 EndBoneIndex = Hierarchy->GetIndex(EndBone);
		if (EndBoneIndex != INDEX_NONE)
		{
			int32 StartBoneIndex = Hierarchy->GetIndex(StartBone);
			if (StartBoneIndex == EndBoneIndex)
			{
				return;
			}

			while (EndBoneIndex != INDEX_NONE)
			{
				BoneIndices.Add(EndBoneIndex);
				if (EndBoneIndex == StartBoneIndex)
				{
					break;
				}
				EndBoneIndex = (*Hierarchy)[EndBoneIndex].ParentIndex;
			}
		}

		if (BoneIndices.Num() < 2)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Didn't find enough bones. You need at least two in the chain!"));
			return;
		}

		Algo::Reverse(BoneIndices);

		BoneSegments.SetNumZeroed(BoneIndices.Num());
		BoneSegments[0] = 0;
		for (int32 Index = 1; Index < BoneIndices.Num(); Index++)
		{
			FVector A = Hierarchy->GetGlobalTransform(BoneIndices[Index - 1]).GetLocation();
			FVector B = Hierarchy->GetGlobalTransform(BoneIndices[Index]).GetLocation();
			BoneSegments[Index] = (A - B).Size();
			ChainLength += BoneSegments[Index];
		}

		Transforms.SetNum(BoneIndices.Num());
		BlendedTransforms.SetNum(BoneIndices.Num());
		return;
	}

	if (BoneIndices.Num() == 0 || ChainLength < SMALL_NUMBER)
	{
		return;
	}

	for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
	{
		Transforms[Index] = Hierarchy->GetGlobalTransform(BoneIndices[Index]);
	}

	for (int32 Index = 0; Index < Transforms.Num(); Index++)
	{
		int32 TargetIndex = Index;
		float Ratio = 0.f;
		float SlidePerBone = -SlideAmount * ChainLength;

		if (SlidePerBone > 0)
		{
			while (SlidePerBone > SMALL_NUMBER && TargetIndex < Transforms.Num() - 1)
			{
				TargetIndex++;
				SlidePerBone -= BoneSegments[TargetIndex];
			}
		}
		else
		{
			while (SlidePerBone < -SMALL_NUMBER && TargetIndex > 0)
			{
				SlidePerBone += BoneSegments[TargetIndex];
				TargetIndex--;
			}
		}

		if (TargetIndex < Transforms.Num() - 1)
		{
			if (BoneSegments[TargetIndex + 1] > SMALL_NUMBER)
			{
				if (SlideAmount < -SMALL_NUMBER)
				{
					Ratio = FMath::Clamp<float>(1.f - FMath::Abs<float>(SlidePerBone / BoneSegments[TargetIndex + 1]), 0.f, 1.f);
				}
				else
				{
					Ratio = FMath::Clamp<float>(SlidePerBone / BoneSegments[TargetIndex + 1], 0.f, 1.f);
				}
			}
		}

		BlendedTransforms[Index] = Transforms[TargetIndex];
		if (TargetIndex < Transforms.Num() - 1 && Ratio > SMALL_NUMBER && Ratio < 1.f - SMALL_NUMBER)
		{
			BlendedTransforms[Index] = FControlRigMathLibrary::LerpTransform(BlendedTransforms[Index], Transforms[TargetIndex + 1], Ratio);
		}
	}

	for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
	{
		if (Index < BoneIndices.Num() - 1)
		{
			FVector CurrentX = BlendedTransforms[Index].GetRotation().GetAxisX();
			FVector DesiredX = BlendedTransforms[Index + 1].GetLocation() - BlendedTransforms[Index].GetLocation();
			FQuat OffsetQuat = FQuat::FindBetweenVectors(CurrentX, DesiredX);
			BlendedTransforms[Index].SetRotation(OffsetQuat * BlendedTransforms[Index].GetRotation());
		}
		Hierarchy->SetGlobalTransform(BoneIndices[Index], BlendedTransforms[Index], bPropagateToChildren);
	}
}
