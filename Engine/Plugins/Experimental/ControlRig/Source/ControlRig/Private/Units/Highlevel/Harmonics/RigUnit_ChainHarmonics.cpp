// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Harmonics/RigUnit_ChainHarmonics.h"
#include "Units/RigUnitContext.h"

void FRigUnit_ChainHarmonics::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchy* Hierarchy = (FRigHierarchy*)(Context.HierarchyReference.Get());
	if (Hierarchy == nullptr)
	{
		return;
	}
	
	if (Context.State == EControlRigState::Init)
	{
		Time = FVector::ZeroVector;
		Bones.Reset();

		int32 RootIndex = Hierarchy->GetIndex(ChainRoot);
		if (RootIndex == INDEX_NONE)
		{
			return;
		}

		Bones.Add(RootIndex);
		TArray<int32> Children;
		Hierarchy->GetChildren(Bones.Last(), Children, false);
		while (Children.Num() > 0)
		{
			Bones.Add(Children[0]);
			Hierarchy->GetChildren(Children[0], Children, false);
		}

		if (Bones.Num() < 2)
		{
			Bones.Reset();
			return;
		}

		Ratio.SetNumZeroed(Bones.Num());
		LocalTip.SetNumZeroed(Bones.Num());
		PendulumTip.SetNumZeroed(Bones.Num());
		PendulumPosition.SetNumZeroed(Bones.Num());
		PendulumVelocity.SetNumZeroed(Bones.Num());
		VelocityLines.SetNumZeroed(Bones.Num() * 2);

		for (int32 Index = 0; Index < Bones.Num(); Index++)
		{
			Ratio[Index] = float(Index) / float(Bones.Num() - 1);

			int32 BoneIndex = Bones[Index];
			LocalTip[Index] = Hierarchy->GetLocalTransform(BoneIndex).GetLocation();
			PendulumPosition[Index] = Hierarchy->GetGlobalTransform(BoneIndex).GetLocation();
		}
		
		for (int32 Index = 0; Index < Bones.Num() - 1; Index++)
		{
			PendulumTip[Index] = LocalTip[Index + 1];
		}
		PendulumTip[PendulumTip.Num() - 1] = PendulumTip[PendulumTip.Num() - 2];

		for (int32 Index = 0; Index < Bones.Num(); Index++)
		{
			PendulumPosition[Index] = Hierarchy->GetGlobalTransform(Bones[Index]).TransformPosition(PendulumTip[Index]);
		}

		return;
	}
	
	if (Bones.Num() == 0)
	{
		return;
	}

	FTransform ParentTransform = FTransform::Identity;
	int32 ParentIndex = Hierarchy->GetParentIndex(Bones[0]);
	if (ParentIndex != INDEX_NONE)
	{
		ParentTransform = Hierarchy->GetGlobalTransform(ParentIndex);
	}

	for (int32 Index = 0;Index < Bones.Num(); Index++)
	{
		FTransform GlobalTransform = Hierarchy->GetLocalTransform(Bones[Index]) * ParentTransform;
		FQuat Rotation = GlobalTransform.GetRotation();

		if (Reach.bEnabled)
		{
			float Ease = FMath::Lerp<float>(Reach.ReachMinimum, Reach.ReachMaximum, Ratio[Index]);;
			Ease = FControlRigMathLibrary::EaseFloat(Ease, Reach.ReachEase);

			FVector Axis = Reach.ReachAxis;
			Axis = ParentTransform.TransformVectorNoScale(Axis);

			FVector ReachDirection = (Reach.ReachTarget - GlobalTransform.GetLocation()).GetSafeNormal();
			ReachDirection = FMath::Lerp<FVector>(Axis, ReachDirection, Ease);

			FQuat ReachRotation = FQuat::FindBetween(Axis, ReachDirection);
			Rotation = (ReachRotation * Rotation).GetNormalized();
		}

		if (Wave.bEnabled)
		{
			float Ease = FMath::Lerp<float>(Wave.WaveMinimum, Wave.WaveMaximum, Ratio[Index]);;
			Ease = FControlRigMathLibrary::EaseFloat(Ease, Wave.WaveEase);

			float Curve = WaveCurve.GetRichCurveConst()->Eval(Ratio[Index], 1.f);

			FVector U = Time + Wave.WaveFrequency * Ratio[Index];

			FVector Noise;
			Noise.X = FMath::PerlinNoise1D(U.X + 132.4f);
			Noise.Y = FMath::PerlinNoise1D(U.Y + 9.2f);
			Noise.Z = FMath::PerlinNoise1D(U.Z + 217.9f);
			Noise = Noise * Wave.WaveNoise * 2.f;
			U = U + Noise;

			FVector Angles;
			Angles.X = FMath::Sin(U.X + Wave.WaveOffset.X);
			Angles.Y = FMath::Sin(U.Y + Wave.WaveOffset.Y);
			Angles.Z = FMath::Sin(U.Z + Wave.WaveOffset.Z);
			Angles = Angles * Wave.WaveAmplitude * Ease * Curve;

			Rotation = Rotation * FQuat(FVector(1.f, 0.f, 0.f), Angles.X);
			Rotation = Rotation * FQuat(FVector(0.f, 1.f, 0.f), Angles.Y);
			Rotation = Rotation * FQuat(FVector(0.f, 0.f, 1.f), Angles.Z);
			Rotation = Rotation.GetNormalized();
		}

		if (Pendulum.bEnabled)
		{
			FQuat NonSimulatedRotation = Rotation;

			float Ease = FMath::Lerp<float>(Pendulum.PendulumMinimum, Pendulum.PendulumMaximum, Ratio[Index]);;
			Ease = FControlRigMathLibrary::EaseFloat(Ease, Pendulum.PendulumEase);

			FVector Stiffness = LocalTip[Index];
			FVector Upvector = Pendulum.UnwindAxis;
			float Length = Stiffness.Size();
			Stiffness = ParentTransform.TransformVectorNoScale(Stiffness);
			Upvector = ParentTransform.TransformVectorNoScale(Upvector);

			FVector Velocity = Pendulum.PendulumGravity;
			Velocity += Stiffness * Pendulum.PendulumStiffness;

			if (Context.DeltaTime > 0.f)
			{
				PendulumVelocity[Index] = FMath::Lerp<FVector>(PendulumVelocity[Index], Velocity, FMath::Clamp<float>(Pendulum.PendulumBlend, 0.f, 0.999f));
				PendulumVelocity[Index] = PendulumVelocity[Index] * Pendulum.PendulumDrag;

				FVector PrevPosition = PendulumPosition[Index];
				PendulumPosition[Index] = PendulumPosition[Index] + PendulumVelocity[Index] * Context.DeltaTime;
				PendulumPosition[Index] = GlobalTransform.GetLocation() + (PendulumPosition[Index] - GlobalTransform.GetLocation()).GetSafeNormal() * Length;
				PendulumVelocity[Index] = (PendulumPosition[Index] - PrevPosition) / Context.DeltaTime;
			}

			VelocityLines[Index * 2 + 0] = PendulumPosition[Index];
			VelocityLines[Index * 2 + 1] = PendulumPosition[Index] + PendulumVelocity[Index] * 0.1f;

			FQuat PendulumRotation = FQuat::FindBetween(Rotation.RotateVector(LocalTip[Index]), PendulumPosition[Index] - GlobalTransform.GetLocation());
			Rotation = (PendulumRotation * Rotation).GetNormalized();

			float Unwind = FMath::Lerp<float>(Pendulum.UnwindMinimum, Pendulum.UnwindMaximum, Ratio[Index]);
			FVector CurrentUpvector = Rotation.RotateVector(Pendulum.UnwindAxis);
			CurrentUpvector = CurrentUpvector - FVector::DotProduct(CurrentUpvector, Rotation.RotateVector(LocalTip[Index]).GetSafeNormal());
			CurrentUpvector = FMath::Lerp<FVector>(Upvector, CurrentUpvector, Unwind);
			FQuat UnwindRotation = FQuat::FindBetween(CurrentUpvector, Upvector);
			Rotation = (UnwindRotation * Rotation).GetNormalized();

			Rotation = FQuat::Slerp(NonSimulatedRotation, Rotation, FMath::Clamp<float>(Ease, 0.f, 1.f));
		}

		GlobalTransform.SetRotation(Rotation);
		Hierarchy->SetGlobalTransform(Bones[Index], GlobalTransform, false);
		ParentTransform = GlobalTransform;
	}

	Time = Time + Speed * Context.DeltaTime;

	if (Context.DrawInterface != nullptr && bDrawDebug)
	{
		HierarchyLine.SetNum(Bones.Num());
		for (int32 Index = 0; Index < Bones.Num(); Index++)
		{
			HierarchyLine[Index] = Hierarchy->GetGlobalTransform(Bones[Index]).GetLocation();
		}

		Context.DrawInterface->DrawLineStrip(DrawWorldOffset, HierarchyLine, FLinearColor::Yellow, 0.f);
		Context.DrawInterface->DrawLines(DrawWorldOffset, VelocityLines, FLinearColor(0.3f, 0.3f, 1.f), 0.f);
		Context.DrawInterface->DrawPoints(DrawWorldOffset, PendulumPosition, 3.f, FLinearColor::Blue);
		return;
	}

}