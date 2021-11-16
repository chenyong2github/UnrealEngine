// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/MotionTrajectoryTypes.h"
#include "Algo/AllOf.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimTypes.h"

#if ENABLE_ANIM_DEBUG
static constexpr int32 DebugTrajectorySampleDisable = 0;
static constexpr int32 DebugTrajectorySampleCount = 1;
static constexpr int32 DebugTrajectorySampleTime = 2;
static constexpr int32 DebugTrajectorySampleDistance = 3;
static constexpr int32 DebugTrajectorySamplePosition = 4;
static constexpr int32 DebugTrajectorySampleVelocity = 5;
static constexpr int32 DebugTrajectorySampleAccel = 6;
static const FVector DebugSampleTypeOffset(0.f, 0.f, 50.f);
static const FVector DebugSampleOffset(0.f, 0.f, 10.f);

TAutoConsoleVariable<int32> CVarMotionTrajectoryDebug(TEXT("a.MotionTrajectory.Debug"), 0, TEXT("Turn on debug drawing for motion trajectory"));
TAutoConsoleVariable<int32> CVarMotionTrajectoryDebugStride(TEXT("a.MotionTrajectory.Stride"), 1, TEXT("Configure the sample stride when displaying information"));
TAutoConsoleVariable<int32> CVarMotionTrajectoryDebugOptions(TEXT("a.MotionTrajectory.Options"), 0,
	TEXT("Toggle motion trajectory sample information:\n 0. Disable Text\n 1. Index\n2. Accumulated Time\n 3. Accumulated Distance\n 4. Position\n 5. Velocity\n 6. Acceleration")
);
#endif

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
	return LinearVelocity.IsNearlyZero()
		&& LinearAcceleration.IsNearlyZero()
		&& Transform.GetTranslation().IsNearlyZero()
		&& FMath::IsNearlyZero(AccumulatedDistance)
		&& FMath::IsNearlyZero(AngularSpeed)
		&& Transform.GetRotation().IsIdentity();
}

FTrajectorySample FTrajectorySample::Lerp(const FTrajectorySample& Sample, float Alpha) const
{
	FTrajectorySample Interp;
	Interp.AccumulatedSeconds = FMath::Lerp(AccumulatedSeconds, Sample.AccumulatedSeconds, Alpha);
	Interp.AccumulatedDistance = FMath::Lerp(AccumulatedDistance, Sample.AccumulatedDistance, Alpha);
	Interp.LinearVelocity = FMath::Lerp(LinearVelocity, Sample.LinearVelocity, Alpha);
	Interp.LinearAcceleration = FMath::Lerp(LinearAcceleration, Sample.LinearAcceleration, Alpha);

	Interp.Transform.Blend(Transform, Sample.Transform, Alpha);
	
	// TODO: this is very simple, more like Lerp than Slerp, can we do better?
	const FVector AngularVelocity = AngularVelocityAxis * AngularSpeed;
	const FVector SampleAngularVelocity = Sample.AngularVelocityAxis * Sample.AngularSpeed;
	const FVector LerpedAngularVelocity = FMath::Lerp(AngularVelocity, SampleAngularVelocity, Alpha);
	const float LerpedAngularSpeed = LerpedAngularVelocity.Size();
	if (LerpedAngularSpeed > SMALL_NUMBER)
	{
		Interp.AngularVelocityAxis = LerpedAngularVelocity / LerpedAngularSpeed;
		Interp.AngularSpeed = LerpedAngularSpeed;
	}
	else
	{
		Interp.AngularVelocityAxis = FVector::ZeroVector;
		Interp.AngularSpeed = 0.0f;
	}

	return Interp;
}

FTrajectorySample FTrajectorySample::SmoothInterp(const FTrajectorySample& PrevSample
	, const FTrajectorySample& Sample
	, const FTrajectorySample& NextSample
	, float Alpha) const
{
	FTrajectorySample Interp;
	Interp.AccumulatedDistance = CubicCRSplineInterpSafe(PrevSample.AccumulatedDistance, AccumulatedDistance, Sample.AccumulatedDistance, NextSample.AccumulatedDistance, Alpha);
	Interp.AccumulatedSeconds = CubicCRSplineInterpSafe(PrevSample.AccumulatedSeconds, AccumulatedSeconds, Sample.AccumulatedSeconds, NextSample.AccumulatedSeconds, Alpha);
	Interp.LinearVelocity = CubicCRSplineInterpSafe(PrevSample.LinearVelocity, LinearVelocity, Sample.LinearVelocity, NextSample.LinearVelocity, Alpha);
	Interp.LinearAcceleration = CubicCRSplineInterpSafe(PrevSample.LinearAcceleration, LinearAcceleration, Sample.LinearAcceleration, NextSample.LinearAcceleration, Alpha);

	Interp.Transform.SetLocation(CubicCRSplineInterpSafe(
		PrevSample.Transform.GetLocation(),
		Transform.GetLocation(),
		Sample.Transform.GetLocation(),
		NextSample.Transform.GetLocation(),
		Alpha));
	FQuat Q0 = PrevSample.Transform.GetRotation().W >= 0.0f ? 
		PrevSample.Transform.GetRotation() : -PrevSample.Transform.GetRotation();
	FQuat Q1 = Transform.GetRotation().W >= 0.0f ? 
		Transform.GetRotation() : -Transform.GetRotation();
	FQuat Q2 = Sample.Transform.GetRotation().W >= 0.0f ? 
		Sample.Transform.GetRotation() : -Sample.Transform.GetRotation();
	FQuat Q3 = NextSample.Transform.GetRotation().W >= 0.0f ? 
		NextSample.Transform.GetRotation() : -NextSample.Transform.GetRotation();

	FQuat T0, T1;
	FQuat::CalcTangents(Q0, Q1, Q2, 0.0f, T0);
	FQuat::CalcTangents(Q1, Q2, Q3, 0.0f, T1);

	Interp.Transform.SetRotation(FQuat::Squad(Q1, T0, Q2, T1, Alpha));

	const FVector V0 = PrevSample.AngularVelocityAxis * PrevSample.AngularSpeed;
	const FVector V1 = AngularVelocityAxis * AngularSpeed;
	const FVector V2 = Sample.AngularVelocityAxis * Sample.AngularSpeed;
	const FVector V3 = NextSample.AngularVelocityAxis * NextSample.AngularSpeed;

	const FVector LerpedAngularVelocity = CubicCRSplineInterpSafe(V0, V1, V2, V3, Alpha);
	const float LerpedAngularSpeed = LerpedAngularVelocity.Size();
	if (LerpedAngularSpeed > SMALL_NUMBER)
	{
		Interp.AngularVelocityAxis = LerpedAngularVelocity / LerpedAngularSpeed;
		Interp.AngularSpeed = LerpedAngularSpeed;
	}
	else
	{
		Interp.AngularVelocityAxis = FVector::ZeroVector;
		Interp.AngularSpeed = 0.0f;
	}

	return Interp;
}

void FTrajectorySample::PrependOffset(const FTransform DeltaTransform, float DeltaSeconds)
{
	AccumulatedSeconds += DeltaSeconds;

	if (FMath::IsNearlyZero(AccumulatedSeconds))
	{
		AccumulatedDistance = 0.0f;
	}
	else
	{
		const float DistanceOffset = DeltaSeconds >= 0.0f ?
			DeltaTransform.GetTranslation().Size() :
			-DeltaTransform.GetTranslation().Size();

		AccumulatedDistance += DistanceOffset;
	}

	Transform *= DeltaTransform;

	LinearVelocity = DeltaTransform.TransformVectorNoScale(LinearVelocity);
	LinearAcceleration = DeltaTransform.TransformVectorNoScale(LinearAcceleration);
	AngularVelocityAxis = DeltaTransform.TransformVectorNoScale(AngularVelocityAxis);
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

void FTrajectorySampleRange::Rotate(const FQuat& Rotation)
{
	for (auto& Sample : Samples)
	{
		Sample.PrependOffset(FTransform(Rotation), 0.0f);
	}
}

void FTrajectorySampleRange::DebugDrawTrajectory(bool bEnable
	, const UWorld* World
	, const FTransform& WorldTransform
	, const FLinearColor PredictionColor
	, const FLinearColor HistoryColor
	, float ArrowScale
	, float ArrowSize
	, float ArrowThickness) const
{
	if (bEnable
#if ENABLE_ANIM_DEBUG
		|| CVarMotionTrajectoryDebug.GetValueOnAnyThread()
#endif
		)
	{
		if (World)
		{
#if ENABLE_ANIM_DEBUG
			const int32 DebugSampleStride = CVarMotionTrajectoryDebugStride.GetValueOnAnyThread();
			const int32 DebugSampleOptions = CVarMotionTrajectoryDebugOptions.GetValueOnAnyThread();
#endif
			for (int32 Idx = 0, Num = Samples.Num(); Idx < Num; Idx++)
			{
				const FVector WorldPosition = WorldTransform.TransformPosition(Samples[Idx].Transform.GetTranslation());
				const FVector WorldForward = 
					(WorldTransform.TransformVectorNoScale(Samples[Idx].Transform.GetRotation().GetAxisX()) *
					 ArrowScale) 
					+ WorldPosition;

				// Interpolate the history and prediction color over the entire trajectory range
				const float ColorLerp = static_cast<float>(Idx) / static_cast<float>(Num);
				const FLinearColor Color = FLinearColor::LerpUsingHSV(PredictionColor, HistoryColor, ColorLerp);

				DrawDebugDirectionalArrow(World, WorldPosition, WorldForward, ArrowSize, Color.ToFColor(true), false, 0.f, 0, ArrowThickness);
#if ENABLE_ANIM_DEBUG
				FString DebugString;
				FString DebugSampleString;
				switch (DebugSampleOptions)
				{
				case DebugTrajectorySampleCount: // Sample Index
					DebugString = "Sample Index:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Idx });
;					break;
				case DebugTrajectorySampleTime: // Sample Accumulated Time
					DebugString = "Sample Time:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Samples[Idx].AccumulatedSeconds });
					break;
				case DebugTrajectorySampleDistance: // Sample Accumulated Distance
					DebugString = "Sample Distance:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Samples[Idx].AccumulatedDistance });
					break;
				case DebugTrajectorySamplePosition: // Sample Position
					DebugString = "Sample Position:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Samples[Idx].Transform.GetLocation().ToCompactString() });
					break;
				case DebugTrajectorySampleVelocity: // Sample Velocity
					DebugString = "Sample Velocity:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Samples[Idx].LinearVelocity.ToCompactString() });
					break;
				case DebugTrajectorySampleAccel: // Sample Acceleration
					DebugString = "Sample Acceleration:";
					DebugSampleString = DebugSampleString.Format(TEXT("{0}"), { Samples[Idx].LinearAcceleration.ToCompactString() });
					break;
				default:
					break;
				}

				// Conditionally display per-sample information against a specified stride
				if (!DebugSampleString.IsEmpty() && !!DebugSampleStride && (Idx % DebugSampleStride == 0))
				{
					// One time debug drawing of the per-sample type description
					if (!DebugString.IsEmpty() && Idx == 0)
					{
						DrawDebugString(World, WorldTransform.GetLocation() + DebugSampleTypeOffset, DebugString, nullptr, FColor::White, 0.f, false, 1.f);
					}

					DrawDebugString(World, WorldForward + DebugSampleOffset, DebugSampleString, nullptr, FColor::White, 0.f, false, 1.f);
				}
#endif
			}
		}
	}
}