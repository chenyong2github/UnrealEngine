#include "ControlRigSplineTypes.h"

void FControlRigSpline::SetControlPoints(const TArray<FVector>& InPoints, const bool forceRebuild)
{
	const int32 ControlPointsCount = InPoints.Num();

	switch (SplineMode)
	{
		case ESplineType::BSpline:
		{
			if (forceRebuild || !BSpline.IsValid() || BSpline->numControlPoints() != ControlPointsCount)
			{
				BSpline = MakeShareable(new tinyspline::BSpline(ControlPointsCount, 3));
			}

			// Update the positions of the control points

			// There's no guarantee that FVector is a tightly packed array of three floats. 
			// We have SIMD versions where we waste a dummy float to align it on a 16 byte boundary,
			// so we need to iterate updating the points one by one.
			for (int i = 0; i < ControlPointsCount; ++i)
			{
				ts_bspline_set_control_point_at(BSpline->data(), i, &InPoints[i].X, nullptr);
			}
			break;
		}
		case ESplineType::Hermite:
		{
			if (forceRebuild || !BSpline.IsValid() || BSpline->numControlPoints() != ControlPointsCount)
			{
				// Create a new BSpline from the control points
				// The first dimension acts as the query parameter, the rest of the dimensions are the XYZ of the control points
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

				BSpline = MakeShareable(new tinyspline::BSpline(ControlPointsCount, 4));
				ts_bspline_interpolate_cubic_natural(ControlPointsArray.GetData(), ControlPointsCount, 4, BSpline->data(), nullptr);
			}
			else
			{
				// We just need to update the positions of the control points
				float* Ctrlp = nullptr;
				ts_bspline_control_point_at(BSpline->data(), 0, &Ctrlp, nullptr);
				for (int32 i = 0; i < ControlPointsCount; ++i)
				{
					Ctrlp[i * 4 + 1] = InPoints[i][0];
					Ctrlp[i * 4 + 2] = InPoints[i][1];
					Ctrlp[i * 4 + 3] = InPoints[i][2];

					ControlPointsArray[i * 4 + 1] = InPoints[i][0];
					ControlPointsArray[i * 4 + 2] = InPoints[i][1];
					ControlPointsArray[i * 4 + 3] = InPoints[i][2];
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

int32 FControlRigSpline::GetControlPoints(TArray<FVector>& OutPoints) const
{
	const size_t Count = BSpline->numControlPoints();
	OutPoints.Reset(Count);

	float* Points;
	ts_bspline_control_points(BSpline->data(), &Points, nullptr);

	switch (SplineMode)
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
	if (!BSpline.IsValid())
	{
		return FVector();
	}

	float ClampedU = FMath::Clamp<float>(InParam, 0.f, 1.f);

	FVector Position;
	std::vector<tinyspline::real> Result;
	switch (SplineMode)
	{
		case ESplineType::BSpline:
		{
			Result = BSpline->eval(ClampedU).result();
			Position[0] = Result[0];
			Position[1] = Result[1];
			Position[2] = Result[2];
			break;
		}
		case ESplineType::Hermite:
		{
			Result = BSpline->bisect(ClampedU).result();
			Position[0] = Result[1];
			Position[1] = Result[2];
			Position[2] = Result[3];
			break;
		}
		default:
		{
			checkNoEntry(); // Unknown Spline Mode
			break;
		}
	}

	return Position;
}
