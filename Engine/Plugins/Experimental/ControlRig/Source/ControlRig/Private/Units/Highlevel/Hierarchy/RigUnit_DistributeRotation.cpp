// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_DistributeRotation.h"
#include "Units/RigUnitContext.h"

FRigUnit_DistributeRotation_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	TArray<int32>& BoneIndices = WorkData.BoneIndices;
	TArray<int32>& BoneRotationA = WorkData.BoneRotationA;
	TArray<int32>& BoneRotationB = WorkData.BoneRotationB;
	TArray<float>& BoneRotationT = WorkData.BoneRotationT;
	TArray<FTransform>& BoneLocalTransforms = WorkData.BoneLocalTransforms;

	if (Context.State == EControlRigState::Init)
	{
		BoneIndices.Reset();
		BoneRotationA.Reset();
		BoneRotationB.Reset();
		BoneRotationT.Reset();
		BoneLocalTransforms.Reset();

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

		BoneRotationA.SetNumZeroed(BoneIndices.Num());
		BoneRotationB.SetNumZeroed(BoneIndices.Num());
		BoneRotationT.SetNumZeroed(BoneIndices.Num());
		BoneLocalTransforms.SetNumZeroed(BoneIndices.Num());

		if (Rotations.Num() < 2)
		{
			return;
		}

		TArray<float> RotationRatios;
		TArray<int32> RotationIndices;

		for (const FRigUnit_DistributeRotation_Rotation& Rotation : Rotations)
		{
			RotationIndices.Add(RotationIndices.Num());
			RotationRatios.Add(FMath::Clamp<float>(Rotation.Ratio, 0.f, 1.f));
		}

		TLess<> Predicate;
		auto Projection = [RotationRatios](int32 Val) -> float
		{
			return RotationRatios[Val];
		};
		Algo::SortBy(RotationIndices, Projection, Predicate);

 		for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
 		{
			float T = 0.f;
			if (BoneIndices.Num() > 1)
			{
				T = float(Index) / float(BoneIndices.Num() - 1);
			}

			if (T <= RotationRatios[RotationIndices[0]])
			{
				BoneRotationA[Index] = BoneRotationB[Index] = RotationIndices[0];
				BoneRotationT[Index] = 0.f;
			}
			else if (T >= RotationRatios[RotationIndices.Last()])
			{
				BoneRotationA[Index] = BoneRotationB[Index] = RotationIndices.Last();
				BoneRotationT[Index] = 0.f;
			}
			else
			{
				for (int32 RotationIndex = 1; RotationIndex < RotationIndices.Num(); RotationIndex++)
				{
					int32 A = RotationIndices[RotationIndex - 1];
					int32 B = RotationIndices[RotationIndex];

					if (FMath::IsNearlyEqual(Rotations[A].Ratio, T))
					{
						BoneRotationA[Index] = BoneRotationB[Index] = A;
						BoneRotationT[Index] = 0.f;
						break;
					}
					else if (FMath::IsNearlyEqual(Rotations[B].Ratio, T))
					{
						BoneRotationA[Index] = BoneRotationB[Index] = B;
						BoneRotationT[Index] = 0.f;
						break;
					}
					else if (Rotations[B].Ratio > T)
					{
						if (FMath::IsNearlyEqual(RotationRatios[A], RotationRatios[B]))
						{
							BoneRotationA[Index] = BoneRotationB[Index] = A;
							BoneRotationT[Index] = 0.f;
						}
						else
						{
							BoneRotationA[Index] = A;
							BoneRotationB[Index] = B;
							BoneRotationT[Index] = (T - RotationRatios[A]) / (RotationRatios[B] - RotationRatios[A]);
							BoneRotationT[Index] = FControlRigMathLibrary::EaseFloat(BoneRotationT[Index], RotationEaseType);
						}
						break;
					}
				}
			}
 		}
		
		return;
	}

	if (BoneIndices.Num() == 0 || Rotations.Num() == 0)
	{
		return;
	}

	FTransform BaseTransform = FTransform::Identity;
	int32 ParentIndex = (*Hierarchy)[BoneIndices[0]].ParentIndex;
	if (ParentIndex != INDEX_NONE)
	{
		BaseTransform = Hierarchy->GetGlobalTransform(ParentIndex);
	}

	for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
	{
		BoneLocalTransforms[Index] = Hierarchy->GetLocalTransform(BoneIndices[Index]);
	}

	for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
	{
		if (BoneRotationA[Index] >= Rotations.Num() ||
			BoneRotationB[Index] >= Rotations.Num())
		{
			continue;
		}

		FQuat Rotation = Rotations[BoneRotationA[Index]].Rotation;
		FQuat RotationB = Rotations[BoneRotationB[Index]].Rotation;
		if (BoneRotationA[Index] != BoneRotationB[Index])
		{
			if (BoneRotationT[Index] > 1.f - SMALL_NUMBER)
			{
				Rotation = RotationB;
			}
			else if (BoneRotationT[Index] > SMALL_NUMBER)
			{
				Rotation = FQuat::Slerp(Rotation, RotationB, BoneRotationT[Index]).GetNormalized();
			}
		}

		BaseTransform = BoneLocalTransforms[Index] * BaseTransform;
		BaseTransform.SetRotation(BaseTransform.GetRotation() * Rotation);
		Hierarchy->SetGlobalTransform(BoneIndices[Index], BaseTransform, bPropagateToChildren);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_DistributeRotation)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(2.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("BoneA"), ERigBoneType::User, FTransform(FVector(2.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneC"), TEXT("BoneB"), ERigBoneType::User, FTransform(FVector(2.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneD"), TEXT("BoneC"), ERigBoneType::User, FTransform(FVector(2.f, 0.f, 0.f)));
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;

	Unit.StartBone = TEXT("Root");
	Unit.EndBone = TEXT("BoneD");
	FRigUnit_DistributeRotation_Rotation Rotation;
	
	Rotation.Rotation = FQuat::Identity;
	Rotation.Ratio = 0.f;
	Unit.Rotations.Add(Rotation);
	Rotation.Rotation = FQuat::Identity;
	Rotation.Ratio = 1.f;
	Unit.Rotations.Add(Rotation);
	Rotation.Rotation = FControlRigMathLibrary::QuatFromEuler(FVector(0.f, 90.f, 0.f), EControlRigRotationOrder::XYZ);
	Rotation.Ratio = 0.5f;
	Unit.Rotations.Add(Rotation);

	Init();

	AddErrorIfFalse(Unit.WorkData.BoneRotationA[0] == 0, TEXT("unexpected bone a"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationB[0] == 0, TEXT("unexpected bone b"));
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.WorkData.BoneRotationT[0], 0.f, 0.001f), TEXT("unexpected bone t"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationA[1] == 0, TEXT("unexpected bone a"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationB[1] == 2, TEXT("unexpected bone b"));
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.WorkData.BoneRotationT[1], 0.5f, 0.001f), TEXT("unexpected bone t"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationA[2] == 2, TEXT("unexpected bone a"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationB[2] == 2, TEXT("unexpected bone b"));
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.WorkData.BoneRotationT[2], 0.f, 0.001f), TEXT("unexpected bone t"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationA[3] == 2, TEXT("unexpected bone a"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationB[3] == 1, TEXT("unexpected bone b"));
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.WorkData.BoneRotationT[3], 0.5f, 0.001f), TEXT("unexpected bone t"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationA[4] == 1, TEXT("unexpected bone a"));
	AddErrorIfFalse(Unit.WorkData.BoneRotationB[4] == 1, TEXT("unexpected bone b"));
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.WorkData.BoneRotationT[4], 0.0f, 0.001f), TEXT("unexpected bone t"));

	Execute();

	FVector Euler = FVector::ZeroVector;
	Euler = FControlRigMathLibrary::EulerFromQuat(BoneHierarchy.GetLocalTransform(0).GetRotation(), EControlRigRotationOrder::XYZ);
	AddErrorIfFalse(FMath::IsNearlyEqual(Euler.Y, 0.f, 0.1f), TEXT("unexpected rotation Y"));
	Euler = FControlRigMathLibrary::EulerFromQuat(BoneHierarchy.GetLocalTransform(1).GetRotation(), EControlRigRotationOrder::XYZ);
	AddErrorIfFalse(FMath::IsNearlyEqual(Euler.Y, 45.f, 0.1f), TEXT("unexpected rotation Y"));
	Euler = FControlRigMathLibrary::EulerFromQuat(BoneHierarchy.GetLocalTransform(2).GetRotation(), EControlRigRotationOrder::XYZ);
	AddErrorIfFalse(FMath::IsNearlyEqual(Euler.Y, 90.f, 0.1f), TEXT("unexpected rotation Y"));
	Euler = FControlRigMathLibrary::EulerFromQuat(BoneHierarchy.GetLocalTransform(3).GetRotation(), EControlRigRotationOrder::XYZ);
	AddErrorIfFalse(FMath::IsNearlyEqual(Euler.Y, 45.f, 0.1f), TEXT("unexpected rotation Y"));
	Euler = FControlRigMathLibrary::EulerFromQuat(BoneHierarchy.GetLocalTransform(4).GetRotation(), EControlRigRotationOrder::XYZ);
	AddErrorIfFalse(FMath::IsNearlyEqual(Euler.Y, 0.f, 0.1f), TEXT("unexpected rotation Y"));

	return true;
}
#endif