// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_FitChainToCurve.h"
#include "Units/RigUnitContext.h"

FRigUnit_FitChainToCurve_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	float& ChainLength = WorkData.ChainLength;
	TArray<FVector>& BonePositions = WorkData.BonePositions;
	TArray<float>& BoneSegments = WorkData.BoneSegments;
	TArray<FVector>& CurvePositions = WorkData.CurvePositions;
	TArray<float>& CurveSegments = WorkData.CurveSegments;
	TArray<int32>& BoneIndices = WorkData.BoneIndices;
	TArray<int32>& BoneRotationA = WorkData.BoneRotationA;
	TArray<int32>& BoneRotationB = WorkData.BoneRotationB;
	TArray<float>& BoneRotationT = WorkData.BoneRotationT;
	TArray<FTransform>& BoneLocalTransforms = WorkData.BoneLocalTransforms;

	if (Context.State == EControlRigState::Init)
	{
		BoneIndices.Reset();
		CurvePositions.Reset();
		BonePositions.Reset();
		BoneSegments.Reset();
		CurveSegments.Reset();
		BoneRotationA.Reset();
		BoneRotationB.Reset();
		BoneRotationT.Reset();
		BoneLocalTransforms.Reset();

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

		BonePositions.SetNumZeroed(BoneIndices.Num());
		BoneSegments.SetNumZeroed(BoneIndices.Num());
		BoneSegments[0] = 0;
		for (int32 Index = 1; Index < BoneIndices.Num(); Index++)
		{
			FVector A = Hierarchy->GetGlobalTransform(BoneIndices[Index - 1]).GetLocation();
			FVector B = Hierarchy->GetGlobalTransform(BoneIndices[Index]).GetLocation();
			BoneSegments[Index] = (A - B).Size();
			ChainLength += BoneSegments[Index];
		}

		BoneRotationA.SetNumZeroed(BoneIndices.Num());
		BoneRotationB.SetNumZeroed(BoneIndices.Num());
		BoneRotationT.SetNumZeroed(BoneIndices.Num());
		BoneLocalTransforms.SetNumZeroed(BoneIndices.Num());

		if (Rotations.Num() > 1)
		{
			TArray<float> RotationRatios;
			TArray<int32> RotationIndices;

			for (const FRigUnit_FitChainToCurve_Rotation& Rotation : Rotations)
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
 		}
		
		return;
	}

	if (BoneIndices.Num() == 0)
	{
		return;
	}

	int32 Samples = FMath::Clamp<int32>(SamplingPrecision, 4, 64);
	if (CurvePositions.Num() != Samples)
	{
		CurvePositions.SetNum(Samples + 1);
		CurveSegments.SetNum(Samples + 1);
	}

	FVector StartTangent = FVector::ZeroVector;
	FVector EndTangent = FVector::ZeroVector;

	float CurveLength = 0.f;
	for (int32 SampleIndex = 0; SampleIndex < Samples; SampleIndex++)
	{
		float T = float(SampleIndex) / float(Samples - 1);
		T = FMath::Lerp<float>(Minimum, Maximum, T);

		FVector Tangent;
		FControlRigMathLibrary::FourPointBezier(Bezier, T, CurvePositions[SampleIndex], Tangent);
		if (SampleIndex == 0)
		{
			StartTangent = Tangent;
		}
		else if (SampleIndex == Samples - 1)
		{
			EndTangent = Tangent;
		}

		if (SampleIndex > 0)
		{
			CurveSegments[SampleIndex] = (CurvePositions[SampleIndex] - CurvePositions[SampleIndex - 1]).Size();
			CurveLength += CurveSegments[SampleIndex];
		}
		else
		{
			CurveSegments[SampleIndex] = 0.f;
		}
	}

	CurvePositions[Samples] = CurvePositions[Samples - 1] + EndTangent * ChainLength;
	CurveSegments[Samples] = ChainLength;

	if (ChainLength < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The chain has no length - all of the bones are in the sample place!"));
		return;
	}

	if (CurveLength < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The curve has no length - all of the points are in the sample place!"));
		return;
	}

	int32 CurveIndex = 1;
	BonePositions[0] = CurvePositions[0];

	for (int32 Index = 1; Index < BoneIndices.Num(); Index++)
	{
		const FVector& LastPosition = BonePositions[Index - 1];

		float BoneLength = BoneSegments[Index];
		switch (Alignment)
		{
			case EControlRigCurveAlignment::Front:
			{
				break;
			}
			case EControlRigCurveAlignment::Stretched:
			{
				BoneLength = BoneLength * CurveLength / ChainLength;
				break;
			}
		}

		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		A = CurvePositions[CurveIndex - 1];
		B = CurvePositions[CurveIndex];

		float DistanceA = (LastPosition - A).Size();
		float DistanceB = (LastPosition - B).Size();
		
		if (DistanceB > BoneLength)
		{
			float Ratio = BoneLength / DistanceB;
			BonePositions[Index] = FMath::Lerp<FVector>(LastPosition, B, Ratio);
			continue;
		}

		while (CurveIndex < CurvePositions.Num() - 1)
		{
			CurveIndex++;
			A = B;
			B = CurvePositions[CurveIndex];
			DistanceA = DistanceB;
			DistanceB = (B - LastPosition).Size();

			if ((DistanceA < BoneLength) != (DistanceB < BoneLength))
			{
				break;
			}
		}

		if (DistanceB < DistanceA)
		{
			FVector TempV = A;
			A = B;
			B = TempV;
			float TempF = DistanceA;
			DistanceA = DistanceB;
			DistanceB = TempF;
		}

		if (FMath::IsNearlyEqual(DistanceA, DistanceB))
		{
			BonePositions[Index] = A;
			continue;
		}

		float Ratio = (BoneLength - DistanceA) / (DistanceB - DistanceA);
		BonePositions[Index] = FMath::Lerp<FVector>(A, B, Ratio);
	}

	for (int32 Index = 0; Index < BoneIndices.Num(); Index++)
	{
		FTransform Transform = Hierarchy->GetGlobalTransform(BoneIndices[Index]);
		Transform.SetTranslation(BonePositions[Index]);

		FVector Target = FVector::ZeroVector;
		if (Index < BoneIndices.Num() - 1)
		{
			Target = BonePositions[Index + 1] - BonePositions[Index];
		}
		else
		{
			Target = BonePositions.Last() - BonePositions[BonePositions.Num() - 2];
		}

		if (!Target.IsNearlyZero() && !PrimaryAxis.IsNearlyZero())
		{
			Target = Target.GetSafeNormal();
			FVector Axis = Transform.TransformVectorNoScale(PrimaryAxis).GetSafeNormal();
			FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
			Transform.SetRotation((Rotation * Transform.GetRotation()).GetNormalized());
		}

		Target = PoleVectorPosition - BonePositions[Index];
		if (!SecondaryAxis.IsNearlyZero())
		{
			if (!PrimaryAxis.IsNearlyZero())
			{
				FVector Axis = Transform.TransformVectorNoScale(PrimaryAxis).GetSafeNormal();
				Target = Target - FVector::DotProduct(Target, Axis) * Axis;
			}

			if (!Target.IsNearlyZero() && !SecondaryAxis.IsNearlyZero())
			{
				Target = Target.GetSafeNormal();
				FVector Axis = Transform.TransformVectorNoScale(SecondaryAxis).GetSafeNormal();
				FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
				Transform.SetRotation((Rotation * Transform.GetRotation()).GetNormalized());
			}
		}

		Hierarchy->SetGlobalTransform(BoneIndices[Index], Transform, bPropagateToChildren && Rotations.Num() == 0);
	}

	if (Rotations.Num() > 0)
	{
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

	if (Context.DrawInterface != nullptr && DebugSettings.bEnabled)
	{
		Context.DrawInterface->DrawBezier(DebugSettings.WorldOffset, Bezier, 0.f, 1.f, DebugSettings.CurveColor, DebugSettings.Scale, 64);
		Context.DrawInterface->DrawPoint(DebugSettings.WorldOffset, Bezier.A, DebugSettings.Scale * 6, DebugSettings.CurveColor);
		Context.DrawInterface->DrawPoint(DebugSettings.WorldOffset, Bezier.B, DebugSettings.Scale * 6, DebugSettings.CurveColor);
		Context.DrawInterface->DrawPoint(DebugSettings.WorldOffset, Bezier.C, DebugSettings.Scale * 6, DebugSettings.CurveColor);
		Context.DrawInterface->DrawPoint(DebugSettings.WorldOffset, Bezier.D, DebugSettings.Scale * 6, DebugSettings.CurveColor);
		Context.DrawInterface->DrawLineStrip(DebugSettings.WorldOffset, CurvePositions, DebugSettings.SegmentsColor, DebugSettings.Scale);
		Context.DrawInterface->DrawPoints(DebugSettings.WorldOffset, CurvePositions, DebugSettings.Scale * 4.f, DebugSettings.SegmentsColor);
		// Context.DrawInterface->DrawPoints(DebugSettings.WorldOffset, CurvePositions, DebugSettings.Scale * 3.f, FLinearColor::Blue);
		// Context.DrawInterface->DrawPoints(DebugSettings.WorldOffset, BonePositions, DebugSettings.Scale * 6.f, DebugSettings.SegmentsColor);
	}
}
