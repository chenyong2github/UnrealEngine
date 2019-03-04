// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RBF/RBFSolver.h"
#include "EngineLogs.h"

FQuat FRBFEntry::AsQuat(int32 Index) const
{
	FQuat Result = FQuat::Identity;

	const int32 BaseIndex = Index * 3;

	if (Values.Num() >= BaseIndex + 3)
	{
		FRotator Rot;
		Rot.Roll	= Values[BaseIndex + 0];
		Rot.Pitch	= Values[BaseIndex + 1];
		Rot.Yaw		= Values[BaseIndex + 2];
		Result = FQuat(Rot);
	}

	return Result;
}

void FRBFEntry::AddFromRotator(const FRotator& InRot)
{
	const int32 BaseIndex = Values.AddUninitialized(3);

	Values[BaseIndex + 0] = InRot.Roll;
	Values[BaseIndex + 1] = InRot.Pitch;
	Values[BaseIndex + 2] = InRot.Yaw;
}

void FRBFEntry::AddFromVector(const FVector& InVector)
{
	const int32 BaseIndex = Values.AddUninitialized(3);

	Values[BaseIndex + 0] = InVector.X;
	Values[BaseIndex + 1] = InVector.Y;
	Values[BaseIndex + 2] = InVector.Z;
}

//////////////////////////////////////////////////////////////////////////

FRBFParams::FRBFParams()
	: TargetDimensions(3)
	, Radius(1.f)
	, Function(ERBFFunctionType::Gaussian)
	, DistanceMethod(ERBFDistanceMethod::Euclidean)
	, TwistAxis(EBoneAxis::BA_X)
	, WeightThreshold(KINDA_SMALL_NUMBER)
	, NormalizeMethod(ERBFNormalizeMethod::OnlyNormalizeAboveOne)
	, MedianReference(FVector(0, 0, 0))
	, MedianMin(45.0f)
	, MedianMax(60.0f)
{

}

FVector FRBFParams::GetTwistAxisVector() const
{
	switch (TwistAxis)
	{
	case BA_X:
	default:
		return FVector(1.f, 0.f, 0.f);
	case BA_Y:
		return FVector(0.f, 1.f, 0.f);
	case BA_Z:
		return FVector(0.f, 0.f, 1.f);
	}
}

//////////////////////////////////////////////////////////////////////////

float FRBFSolver::FindDistanceBetweenEntries(const FRBFEntry& A, const FRBFEntry& B, const FRBFParams& Params, ERBFDistanceMethod OverrideMethod)
{
	check(A.GetDimensions() == B.GetDimensions());

	ERBFDistanceMethod DistanceMethod = OverrideMethod == ERBFDistanceMethod::DefaultMethod ? Params.DistanceMethod : OverrideMethod;

	// Simple n-dimensional distance
	if (DistanceMethod == ERBFDistanceMethod::Euclidean)
	{
		float DistSqr = 0.f;

		for (int32 i = 0; i < A.Values.Num(); i++)
		{
			DistSqr += FMath::Square(A.Values[i] - B.Values[i]);
		}

		return FMath::Sqrt(DistSqr);
	}
	// Treat values as sequence of eulers - find quat distance between each pair, then sqrt-sum-of-squares of those
	else if (DistanceMethod == ERBFDistanceMethod::Quaternion)
	{
		float DistSqr = 0.f;

		const int32 NumRots = A.GetDimensions() / 3;
		for (int32 RotIdx = 0; RotIdx < NumRots; RotIdx++)
		{
			float RadDist = A.AsQuat(RotIdx).AngularDistance(B.AsQuat(RotIdx));
			DistSqr += FMath::Square(FMath::RadiansToDegrees(RadDist));
		}

		return FMath::Sqrt(DistSqr);
	}
	// Treat values as sequence of eulers - find 'swing' distance between each pair, then sqrt-sum-of-squares of those
	else if(DistanceMethod == ERBFDistanceMethod::SwingAngle || DistanceMethod == ERBFDistanceMethod::DefaultMethod)
	{
		float DistSqr = 0.f;

		const int32 NumRots = A.GetDimensions() / 3;
		for (int32 RotIdx = 0; RotIdx < NumRots; RotIdx++)
		{
			FVector TwistVector = Params.GetTwistAxisVector();
			FVector VecA = A.AsQuat(RotIdx).RotateVector(TwistVector);
			FVector VecB = B.AsQuat(RotIdx).RotateVector(TwistVector);

			const float Dot = FVector::DotProduct(VecA, VecB);
			const float RadDist = FMath::Acos(Dot);
			DistSqr += FMath::Square(FMath::RadiansToDegrees(RadDist));
		}

		return FMath::Sqrt(DistSqr);
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown ERBFDistanceMethod"));
		return 0.f;
	}
}

void FRBFSolver::Solve(const FRBFParams& Params, const TArray<FRBFTarget>& Targets, const FRBFEntry& Input, TArray<FRBFOutputWeight>& OutputWeights)
{
	if (Params.TargetDimensions != Input.GetDimensions())
	{
		UE_LOG(LogAnimation, Warning, TEXT("Input dimensionality is %d, expected %d"), Input.GetDimensions(), Params.TargetDimensions);
		return;
	}

	TArray<float> AllWeights;
	AllWeights.AddZeroed(Targets.Num());

	float TotalWeight = 0.f; // Keep track of total weight generated, to renormalize at the end

	// Iterate over each pose, adding its contribution
	for (int32 TargetIdx = 0; TargetIdx < Targets.Num(); TargetIdx++)
	{
		const FRBFTarget& Target = Targets[TargetIdx];
		ERBFFunctionType FunctionType = Target.FunctionType == ERBFFunctionType::DefaultFunction ? Params.Function : Target.FunctionType;

		// Find distance
		const float Distance = FindDistanceBetweenEntries(Target, Input, Params, Target.DistanceMethod);
		const float Scaling = GetRadiusForTarget(Target, Params);
		const float X = Distance / Scaling;

		// Evaluate radial basis function to find weight
		float Weight = 0.f;
		if (FunctionType == ERBFFunctionType::Gaussian)
		{
			Weight = FMath::Exp(-(X * X));
		}
		else if (FunctionType == ERBFFunctionType::Exponential)
		{
			Weight = 1.f / FMath::Exp(X);
		}
		else if (FunctionType == ERBFFunctionType::Linear || FunctionType == ERBFFunctionType::DefaultFunction)
		{
			Weight = FMath::Max(1.f - X, 0.f);
		}
		else if (FunctionType == ERBFFunctionType::Cubic)
		{
			Weight = FMath::Max(1.f - (X * X * X), 0.f);
		}
		else if (FunctionType == ERBFFunctionType::Quintic)
		{
			Weight = FMath::Max(1.f - (X * X * X * X * X), 0.f);
		}

		// Apply custom curve if desired
		if (Target.bApplyCustomCurve)
		{
			Weight = Target.CustomCurve.Eval(Weight, Weight); // default is un-mapped Weight
		}

		// Add to array of all weights. Don't threshold yet, wait for normalization step.
		AllWeights[TargetIdx] = Weight;

		// Add weight to total
		TotalWeight += Weight;
	}

	// Only normalize and apply if we got some kind of weight
	if (TotalWeight > KINDA_SMALL_NUMBER)
	{
		float WeightScale = 1.f;
		if (TotalWeight > 1.f)
		{
			WeightScale = 1.f / TotalWeight;
		}
		else
		{
			switch (Params.NormalizeMethod)
			{
				case ERBFNormalizeMethod::OnlyNormalizeAboveOne:
				{
					break;
				}
				case ERBFNormalizeMethod::AlwaysNormalize:
				{
					WeightScale = 1.f / TotalWeight;
					break;
				}
				case ERBFNormalizeMethod::NormalizeWithinMedian:
				{
					if (Params.MedianMax < Params.MedianMin)
					{
						break;
					}

					FRBFEntry MedianEntry;
					while (Input.GetDimensions() > MedianEntry.GetDimensions())
					{
						MedianEntry.AddFromVector(Params.MedianReference);
					}
					
					float MedianDistance = FindDistanceBetweenEntries(Input, MedianEntry, Params);
					if (MedianDistance > Params.MedianMax)
					{
						break;
					}
					if (MedianDistance <= Params.MedianMin)
					{
						WeightScale = 1.f / TotalWeight;
						break;
					}

					float Bias = FMath::Clamp<float>((MedianDistance - Params.MedianMin) / (Params.MedianMax - Params.MedianMin), 0.f, 1.f);
					WeightScale = FMath::Lerp<float>(1.f / TotalWeight, 1.f, Bias);
					break;
				}
			}
		}
		
		/// TotalWeight : (Params.bNormalizeWeightsBelowSumOfOne ? 1.f / TotalWeight : 1.f);
		for (int32 TargetIdx = 0; TargetIdx < Targets.Num(); TargetIdx++)
		{
			float NormalizedWeight = AllWeights[TargetIdx] * WeightScale;

			// If big enough, add to output list
			if (NormalizedWeight > Params.WeightThreshold)
			{
				OutputWeights.Add(FRBFOutputWeight(TargetIdx, NormalizedWeight));
			}
		}
	}
}


bool FRBFSolver::FindTargetNeighbourDistances(const FRBFParams& Params, const TArray<FRBFTarget>& Targets, TArray<float>& NeighbourDists)
{
	const int32 NumTargets = Targets.Num();

	NeighbourDists.Empty();
	NeighbourDists.AddZeroed(NumTargets);

	if (NumTargets > 1)
	{
		// Iterate over targets
		for (int32 TargetIdx = 0; TargetIdx < NumTargets; TargetIdx++)
		{
			float& NearestDist = NeighbourDists[TargetIdx];
			NearestDist = BIG_NUMBER; // init to large value

			for (int32 OtherTargetIdx = 0; OtherTargetIdx < NumTargets; OtherTargetIdx++)
			{
				if (OtherTargetIdx != TargetIdx) // If not ourself..
				{
					// Get distance between poses
					float Dist = FindDistanceBetweenEntries(Targets[TargetIdx], Targets[OtherTargetIdx], Params, Targets[TargetIdx].DistanceMethod);
					NearestDist = FMath::Min(Dist, NearestDist);
				}
			}

			// Avoid zero dist if poses are all on top of each other
			NearestDist = FMath::Max(NearestDist, KINDA_SMALL_NUMBER);
		}

		return true;
	}
	else
	{
		return false;
	}
}

float FRBFSolver::GetRadiusForTarget(const FRBFTarget& Target, const FRBFParams& Params)
{
	return FMath::Max(Params.Radius * Target.ScaleFactor, KINDA_SMALL_NUMBER);
}
