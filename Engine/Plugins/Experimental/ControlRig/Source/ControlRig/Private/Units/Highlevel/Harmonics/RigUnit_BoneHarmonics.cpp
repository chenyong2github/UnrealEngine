// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Harmonics/RigUnit_BoneHarmonics.h"
#include "Units/RigUnitContext.h"

FRigUnit_BoneHarmonics_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	TArray<int32>& BoneIndices = WorkData.BoneIndices;
	FVector& WaveTime = WorkData.WaveTime;

	if (Context.State == EControlRigState::Init ||
		BoneIndices.Num() != Bones.Num())
	{
		BoneIndices.Reset();
		WaveTime = FVector::ZeroVector;
		if (Hierarchy)
		{
			for (const FRigUnit_BoneHarmonics_BoneTarget& Bone : Bones)
			{
				BoneIndices.Add(Hierarchy->GetIndex(Bone.Bone));
			}
		}
	}

	for (int32 BoneIndex = 0; BoneIndex < BoneIndices.Num(); BoneIndex++)
	{
		if (BoneIndices[BoneIndex] != INDEX_NONE)
		{
			float Ease = FMath::Clamp<float>(Bones[BoneIndex].Ratio, 0.f, 1.f);
			Ease = FControlRigMathLibrary::EaseFloat(Ease, WaveEase);
			Ease = FMath::Lerp<float>(WaveMinimum, WaveMaximum, Ease);

			FVector U = WaveTime + WaveFrequency * Bones[BoneIndex].Ratio;

			FVector Noise;
			Noise.X = FMath::PerlinNoise1D(U.X + 132.4f);
			Noise.Y = FMath::PerlinNoise1D(U.Y + 9.2f);
			Noise.Z = FMath::PerlinNoise1D(U.Z + 217.9f);
			Noise = Noise * WaveNoise * 2.f;
			U = U + Noise;

			FVector Angles;
			Angles.X = FMath::Sin(U.X + WaveOffset.X);
			Angles.Y = FMath::Sin(U.Y + WaveOffset.Y);
			Angles.Z = FMath::Sin(U.Z + WaveOffset.Z);
			Angles = Angles * WaveAmplitude * Ease;

			FQuat Rotation = FControlRigMathLibrary::QuatFromEuler(Angles, RotationOrder);

			FTransform Transform = Hierarchy->GetGlobalTransform(BoneIndices[BoneIndex]);
			Transform.SetRotation(Transform.GetRotation() * Rotation);
			Hierarchy->SetGlobalTransform(BoneIndices[BoneIndex], Transform, bPropagateToChildren);
		}
	}

	WaveTime += WaveSpeed * Context.DeltaTime;
}
