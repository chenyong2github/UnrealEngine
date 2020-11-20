// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSplineTypes.h"

void FControlRigSpline::SetControlPoints(const TArray<FVector>& InPoints, const ESplineType SplineMode, const int32 SamplesPerSegment)
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
	if (!bSplineModeChanged && !bControlPointsChanged && !bSamplesCountChanged)
	{
		return;
	}

	SplineData->ControlPoints = InPoints;
	SplineData->SplineMode = SplineMode;
	SplineData->SamplesPerSegment = SamplesPerSegment;

	// If we need to update the spline because the controls points have changed, or the spline mode has changed
	if (bControlPointsChanged || bSplineModeChanged)
	{	
		switch (SplineMode)
		{
			case ESplineType::BSpline:
			{
				if (bSplineModeChanged || SplineData->Spline.numControlPoints() != ControlPointsCount)
				{
					SplineData->Spline = tinyspline::BSpline(ControlPointsCount, 3);
				}

				// Update the positions of the control points

				// There's no guarantee that FVector is a tightly packed array of three floats. 
				// We have SIMD versions where we waste a dummy float to align it on a 16 byte boundary,
				// so we need to iterate updating the points one by one.
				for (int32 i = 0; i < ControlPointsCount; ++i)
				{
					ts_bspline_set_control_point_at(SplineData->Spline.data(), i, &InPoints[i].X, nullptr);
				}

				break;
			}
			case ESplineType::Hermite:
			{
				if (bSplineModeChanged || SplineData->Spline.numControlPoints() != ControlPointsCount)
				{
					// Create a new BSpline from the control points
					// The first dimension acts as the query parameter, the rest of the dimensions are the XYZ of the control points
					TArray<float> ControlPointsArray;
					ControlPointsArray.SetNumUninitialized(ControlPointsCount * 4, false);
				
					for (int32 i = 0; i < ControlPointsCount; ++i)
					{
						// The control point with weight 0 is the second point
						// The control point with weight 1 is the second to last point
						// This way the first and last point act as tangents
						ControlPointsArray[i * 4 + 0] = (i - 1) / (float)(ControlPointsCount - 3);

						ControlPointsArray[i * 4 + 1] = InPoints[i][0];
						ControlPointsArray[i * 4 + 2] = InPoints[i][1];
						ControlPointsArray[i * 4 + 3] = InPoints[i][2];
					}

					SplineData->Spline = tinyspline::BSpline(ControlPointsCount, 4);
					ts_bspline_interpolate_cubic_natural(ControlPointsArray.GetData(), ControlPointsCount, 4, SplineData->Spline.data(), nullptr);
				}
				else
				{
					// We just need to update the positions of the control points
					float* Ctrlp = nullptr;
					ts_bspline_control_point_at(SplineData->Spline.data(), 0, &Ctrlp, nullptr);
					for (int32 i = 0; i < ControlPointsCount; ++i)
					{
						Ctrlp[i * 4 + 1] = InPoints[i][0];
						Ctrlp[i * 4 + 2] = InPoints[i][1];
						Ctrlp[i * 4 + 3] = InPoints[i][2];
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
	}

	// If curve has changed, or sample count has changed, recompute the cache
	if (bControlPointsChanged || bSplineModeChanged || bSamplesCountChanged)
	{
		switch (SplineMode)
		{
			case ESplineType::BSpline:
			{
				// Cache sample positions of the spline
				float* SamplesPtr = nullptr;
				size_t ActualSamplesPerSegment = 0;
				ts_bspline_sample(SplineData->Spline.data(), (ControlPointsCount - 1) * SamplesPerSegment, &SamplesPtr, &ActualSamplesPerSegment, nullptr);
				SplineData->SamplesArray.SetNumUninitialized((ControlPointsCount - 1) * SamplesPerSegment, false);
				for (int32 i = 0; i < ControlPointsCount - 1; ++i)
				{
					for (int32 j = 0; j < SamplesPerSegment; ++j)
					{
						memcpy(&SplineData->SamplesArray[i * SamplesPerSegment + j].X, SamplesPtr + (i * SamplesPerSegment + j) * 3, sizeof(float) * 3);
					}
				}

				// tinySpline will allocate the samples array, but does not free that memory. We need to take care of that.
				free(SamplesPtr);

				break;
			}
			case ESplineType::Hermite:
			{
				// Cache sample positions of the spline
				float* SamplesPtr = nullptr;
				size_t ActualSamplesPerSegment = 0;
				ts_bspline_sample(SplineData->Spline.data(), (ControlPointsCount - 1) * SamplesPerSegment, &SamplesPtr, &ActualSamplesPerSegment, nullptr);
				SplineData->SamplesArray.SetNumUninitialized((ControlPointsCount - 3) * SamplesPerSegment);
				for (int32 i = 0; i < ControlPointsCount - 3; ++i)
				{
					for (int32 j = 0; j < SamplesPerSegment; ++j)
					{
						memcpy(&SplineData->SamplesArray[i * SamplesPerSegment + j].X, SamplesPtr + ((i + 1) * SamplesPerSegment + j) * 4 + 1, sizeof(float) * 3);
					}
				}

				// tinySpline will allocate the samples array, but does not free that memory. We need to take care of that.
				free(SamplesPtr);

				break;
			}
			default:
			{
				checkNoEntry(); // Unknown Spline Mode
				break;
			}
		}

		// Cache accumulated length at sample array
		SplineData->AccumulatedLenth.SetNumUninitialized(SplineData->SamplesArray.Num(), false);
		SplineData->AccumulatedLenth[0] = 0.f;
		for (int32 i = 1; i < SplineData->SamplesArray.Num(); ++i)
		{
			SplineData->AccumulatedLenth[i] = SplineData->AccumulatedLenth[i - 1] + FVector::Distance(SplineData->SamplesArray[i - 1], SplineData->SamplesArray[i]);
		}
	}	
}

int32 FControlRigSpline::GetControlPoints(TArray<FVector>& OutPoints) const
{
	const size_t Count = SplineData->Spline.numControlPoints();
	OutPoints.Reset(Count);

	float* Points;
	ts_bspline_control_points(SplineData->Spline.data(), &Points, nullptr);

	switch (SplineData->SplineMode)
	{
		case ESplineType::BSpline:
		{
			for (int32 i = 0; i < Count; ++i)
			{
				int32 index = i*3;
				OutPoints[i].Set(Points[index + 0], Points[index + 1], Points[index + 2]);
			}
			break;
		}
		case ESplineType::Hermite:
		{
			for (int32 i = 0; i < Count; ++i)
			{
				int32 index = i * 3;
				OutPoints[i].Set(Points[index + 1], Points[index + 2], Points[index + 3]);
			}
			break;
		}
		default:
		{
			checkNoEntry();
			break;
		}
	}

	return Count;
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
