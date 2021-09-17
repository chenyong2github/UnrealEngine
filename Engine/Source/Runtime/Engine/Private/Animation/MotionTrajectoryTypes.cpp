// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/MotionTrajectoryTypes.h"
#include "Algo/AllOf.h"

namespace
{
	template<class U> static inline U CubicCRSplineInterpSafe(const U& P0, const U& P1, const U& P2, const U& P3, const float I, const float A = 0.5f)
	{
		float D1;
		float D2;
		float D3;

		if constexpr (TIsFloatingPoint<U>::Value)
		{
			D1 = FMath::Abs(P1 - P0);
			D2 = FMath::Abs(P2 - P1);
			D3 = FMath::Abs(P3 - P2);
		}
		else
		{
			D1 = static_cast<float>(FVector::Distance(P0, P1));
			D2 = static_cast<float>(FVector::Distance(P2, P1));
			D3 = static_cast<float>(FVector::Distance(P3, P2));
		}

		const float T0 = 0.f;
		const float T1 = T0 + FMath::Pow(D1, A);
		const float T2 = T1 + FMath::Pow(D2, A);
		const float T3 = T2 + FMath::Pow(D3, A);

		return FMath::CubicCRSplineInterpSafe(P0, P1, P2, P3, T0, T1, T2, T3, FMath::Lerp(T1, T2, I));
	}
}

bool FTrajectorySample::IsZeroSample() const
{
	// AccumulatedTime is specifically omitted here to allow for the zero sample semantic across an entire trajectory range
	return LocalLinearVelocity.IsNearlyZero()
		&& LocalLinearAcceleration.IsNearlyZero()
		&& Position.IsNearlyZero()
		&& FMath::IsNearlyZero(AccumulatedDistance);
}

FTrajectorySample FTrajectorySample::Lerp(const FTrajectorySample& Sample, float Alpha) const
{
	FTrajectorySample Interp;
	Interp.AccumulatedSeconds = FMath::Lerp(AccumulatedSeconds, Sample.AccumulatedSeconds, Alpha);
	Interp.AccumulatedDistance = FMath::Lerp(AccumulatedDistance, Sample.AccumulatedDistance, Alpha);
	Interp.Position = FMath::Lerp(Position, Sample.Position, Alpha);
	Interp.LocalLinearVelocity = FMath::Lerp(LocalLinearVelocity, Sample.LocalLinearVelocity, Alpha);
	Interp.LocalLinearAcceleration = FMath::Lerp(LocalLinearAcceleration, Sample.LocalLinearAcceleration, Alpha);
	return Interp;
}

FTrajectorySample FTrajectorySample::CubicCRSplineInterp(const FTrajectorySample& PrevSample
	, const FTrajectorySample& Sample
	, const FTrajectorySample& NextSample
	, float Alpha) const
{
	FTrajectorySample Interp;
	Interp.AccumulatedDistance = CubicCRSplineInterpSafe(PrevSample.AccumulatedDistance, AccumulatedDistance, Sample.AccumulatedDistance, NextSample.AccumulatedDistance, Alpha);
	Interp.AccumulatedSeconds = CubicCRSplineInterpSafe(PrevSample.AccumulatedSeconds, AccumulatedSeconds, Sample.AccumulatedSeconds, NextSample.AccumulatedSeconds, Alpha);
	Interp.Position = CubicCRSplineInterpSafe(PrevSample.Position, Position, Sample.Position, NextSample.Position, Alpha);
	Interp.LocalLinearVelocity = CubicCRSplineInterpSafe(PrevSample.LocalLinearVelocity, LocalLinearVelocity, Sample.LocalLinearVelocity, NextSample.LocalLinearVelocity, Alpha);
	Interp.LocalLinearAcceleration = CubicCRSplineInterpSafe(PrevSample.LocalLinearAcceleration, LocalLinearAcceleration, Sample.LocalLinearAcceleration, NextSample.LocalLinearAcceleration, Alpha);
	return Interp;
}

bool FTrajectorySampleRange::HasSamples() const
{
	return !Samples.IsEmpty();
}

bool FTrajectorySampleRange::HasOnlyZeroSamples() const
{
	return Algo::AllOf(Samples, [](const FTrajectorySample& Sample)
		{
			return Sample.IsZeroSample();
		});
}

void FTrajectorySampleRange::RemoveHistory()
{
	Samples.RemoveAll([](const FTrajectorySample& Sample)
		{
			return Sample.AccumulatedSeconds < 0.f;
		});
}