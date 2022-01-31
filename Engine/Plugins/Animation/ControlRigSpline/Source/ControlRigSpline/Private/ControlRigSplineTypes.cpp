// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSplineTypes.h"

FControlRigSpline::FControlRigSpline(const FControlRigSpline& InOther)
{
	SplineData = InOther.SplineData;
}

FControlRigSpline& FControlRigSpline::operator=(const FControlRigSpline& InOther)
{
	SplineData = InOther.SplineData;
	return *this;
}

void FControlRigSpline::SetControlPoints(const TArrayView<const FVector>& InPoints, const ESplineType SplineMode, const int32 SamplesPerSegment, const float Compression, const float Stretch)
{
	const int32 ControlPointsCount = InPoints.Num();
	if (ControlPointsCount < 4)
	{
		return;
	}

	if (!SplineData.IsValid())
	{
		SplineData = MakeShared<FControlRigSplineImpl>();
	}

	bool bControlPointsChanged = InPoints != SplineData->ControlPoints;
	bool bSplineModeChanged = SplineMode != SplineData->SplineMode;
	bool bSamplesCountChanged = SamplesPerSegment != SplineData->SamplesPerSegment;
	bool bNumControlPointsChanged = SplineData->ControlPoints.Num() != ControlPointsCount;
	bool bStretchChanged = Stretch != SplineData->Stretch || Compression != SplineData->Compression;
	if (!bSplineModeChanged && !bControlPointsChanged && !bSamplesCountChanged && !bStretchChanged)
	{
		return;
	}

	SplineData->ControlPoints = InPoints;
	SplineData->SplineMode = SplineMode;
	SplineData->SamplesPerSegment = SamplesPerSegment;
	SplineData->Compression = Compression;
	SplineData->Stretch = Stretch;

	// If we need to update the spline because the controls points have changed, or the spline mode has changed
	if (bControlPointsChanged || bSplineModeChanged)
	{	
		switch (SplineMode)
		{
			case ESplineType::BSpline:
			{
				if (bSplineModeChanged || bNumControlPointsChanged)
				{
					SplineData->Spline = tinyspline::BSpline(ControlPointsCount, 3);
				}

				// Update the positions of the control points

				// There's no guarantee that FVector is a tightly packed array of three floats. 
				// We have SIMD versions where we waste a dummy float to align it on a 16 byte boundary,
				// so we need to iterate updating the points one by one.
				for (int32 i = 0; i < ControlPointsCount; ++i)
				{
					FVector Point = InPoints[i];
					ts_bspline_set_control_point_at(SplineData->Spline.data(), i, &Point.X, nullptr);
				}

				break;
			}
			case ESplineType::Hermite:
			{
				break;
			}
			default:
			{
				checkNoEntry(); // Unknown Spline Mode
				break;
			}
		}
	}

	// If curve has changed, or sample count has changed, recompute the cache
	if (bControlPointsChanged || bSplineModeChanged || bSamplesCountChanged || bStretchChanged)
	{
		switch (SplineMode)
		{
			case ESplineType::BSpline:
			{
				// Cache sample positions of the spline
				FVector::FReal* SamplesPtr = nullptr;
				size_t ActualSamplesPerSegment = 0;
				ts_bspline_sample(SplineData->Spline.data(), (ControlPointsCount - 1) * SamplesPerSegment, &SamplesPtr, &ActualSamplesPerSegment, nullptr);
				SplineData->SamplesArray.SetNumUninitialized((ControlPointsCount - 1) * SamplesPerSegment, false);
				for (int32 i = 0; i < ControlPointsCount - 1; ++i)
				{
					for (int32 j = 0; j < SamplesPerSegment; ++j)
					{
						SplineData->SamplesArray[i * SamplesPerSegment + j].X = SamplesPtr[(i * SamplesPerSegment + j) * 3];
						SplineData->SamplesArray[i * SamplesPerSegment + j].Y = SamplesPtr[(i * SamplesPerSegment + j) * 3 + 1];
						SplineData->SamplesArray[i * SamplesPerSegment + j].Z = SamplesPtr[(i * SamplesPerSegment + j) * 3 + 2];
					}
				}

				// tinySpline will allocate the samples array, but does not free that memory. We need to take care of that.
				free(SamplesPtr);

				break;
			}
			case ESplineType::Hermite:
			{
				SplineData->SamplesArray.SetNumUninitialized((ControlPointsCount - 1) * SamplesPerSegment);
				for (int32 i = 0; i < ControlPointsCount - 1; ++i)
				{
					const FVector P0 = (i > 0) ? InPoints[i-1] : 2*InPoints[0] - InPoints[1];
					const FVector& P1 = InPoints[i];
					const FVector& P2 = InPoints[i+1];
					const FVector P3 = (i + 2 < ControlPointsCount) ? InPoints[i+2] : 2*InPoints.Last() - InPoints[ControlPointsCount-2];

					// https://www.cs.cmu.edu/~fp/courses/graphics/asst5/catmullRom.pdf
					float Tension = 0.5f;
					FVector M1 = Tension * (P2 - P0);
					FVector M2 = Tension * (P3 - P1);

					float Dt = 1.f / (float) SamplesPerSegment;
					if (i == ControlPointsCount - 2)
					{
						Dt = 1.f / (float) (SamplesPerSegment - 1);
					}
					for (int32 j = 0; j < SamplesPerSegment; ++j)
					{
						// https://en.wikipedia.org/wiki/Cubic_Hermite_spline#Catmull–Rom_spline
						const float T = j  * Dt;
						const float T2 = T*T;
						const float T3 = T2*T;
	
						float H00 = 2*T3 - 3*T2 + 1;
						float H10 = T3 - 2*T2 + T;
						float H01 = -2*T3 + 3*T2;
						float H11 = T3 - T2;

						SplineData->SamplesArray[i * SamplesPerSegment + j] = H00*P1 + H10*M1 + H01*P2 + H11*M2;						
					}
				}
				break;
			}
			default:
			{
				checkNoEntry(); // Unknown Spline Mode
				break;
			}
		}

		// Correct length of samples
		if (!bSplineModeChanged && !bSamplesCountChanged && !bNumControlPointsChanged)
		{
			if (SplineData->InitialLengths.Num() > 0)
			{
				TArray<FVector> SamplesBeforeCorrect = SplineData->SamplesArray;
				for (int32 i = 0; i < ControlPointsCount - 1; ++i)
				{
					for (int32 j = (i == 0) ? 1 : 0; j < SamplesPerSegment; ++j)
					{
						// Get direction from samples before correction
						FVector Dir = SamplesBeforeCorrect[i * SamplesPerSegment + j] - SamplesBeforeCorrect[i * SamplesPerSegment + j - 1];
						Dir.Normalize();

						float InitialLength = SplineData->InitialLengths[i * SamplesPerSegment + j - 1];
						// Current length as the projection on the Dir vector (might be negative)
						float CurrentLength = (SplineData->SamplesArray[i * SamplesPerSegment + j] - SplineData->SamplesArray[i * SamplesPerSegment + j - 1]).Dot(Dir);
						float FixedLength = FMath::Clamp(CurrentLength,
							(Compression > 0.f) ? InitialLength * Compression : CurrentLength,
							(Stretch > 0.f) ? InitialLength * Stretch : CurrentLength);

						SplineData->SamplesArray[i * SamplesPerSegment + j] = SplineData->SamplesArray[i * SamplesPerSegment + j - 1] + Dir * FixedLength;
					}
				}
			}
		}

		// Cache accumulated length at sample array
		{
			SplineData->AccumulatedLenth.SetNumUninitialized(SplineData->SamplesArray.Num(), false);
			SplineData->AccumulatedLenth[0] = 0.f;
			if (bSplineModeChanged || bSamplesCountChanged || bNumControlPointsChanged)
			{
				SplineData->InitialLengths.SetNumUninitialized(SplineData->SamplesArray.Num() - 1);
			}
			for (int32 i = 1; i < SplineData->SamplesArray.Num(); ++i)
			{
				float CurrentLength = FVector::Distance(SplineData->SamplesArray[i - 1], SplineData->SamplesArray[i]);
				if (bSplineModeChanged || bSamplesCountChanged || bNumControlPointsChanged)
				{
					SplineData->InitialLengths[i-1] = CurrentLength;
				}
				SplineData->AccumulatedLenth[i] = SplineData->AccumulatedLenth[i - 1] + CurrentLength;
			}
		}
	}	
}

FVector FControlRigSpline::PositionAtParam(const float InParam) const
{
	if (!SplineData.IsValid())
	{
		return FVector();
	}

	float ClampedU = FMath::Clamp<float>(InParam, 0.f, 1.f);

	const int32 LastIndex = SplineData->SamplesArray.Num() - 1;
	float fIndexPrev = ClampedU * LastIndex;	
	int32 IndexPrev = FMath::Floor<int32>(fIndexPrev);
	float ULocal = fIndexPrev - IndexPrev;
	int32 IndexNext = (IndexPrev < LastIndex) ? IndexPrev + 1 : IndexPrev;

	return SplineData->SamplesArray[IndexPrev] * (1.f - ULocal) + SplineData->SamplesArray[IndexNext] * ULocal;
}

FVector FControlRigSpline::TangentAtParam(const float InParam) const
{
	if (!SplineData.IsValid())
	{
		return FVector();
	}

	const float ClampedU = FMath::Clamp<float>(InParam, 0.f, 1.f);

	int32 IndexPrev = ClampedU * (SplineData->SamplesArray.Num()-2);
	return SplineData->SamplesArray[IndexPrev+1] - SplineData->SamplesArray[IndexPrev];
}
