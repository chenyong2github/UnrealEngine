// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGLandscapeSplineData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"

#include "Landscape.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplineControlPoint.h"

namespace PCGLandscapeDataHelpers
{
	// This function assumes that the A-B segment has a "1" density, while the C-D segment has a "0" density
	FVector::FReal GetDensityInQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& P)
	{
		FVector BaryABC = FMath::ComputeBaryCentric2D(P, A, B, C);

		if (BaryABC.X >= 0 && BaryABC.Y >= 0 && BaryABC.Z >= 0)
		{
			return 1.0f - BaryABC.Z;
		}

		FVector BaryACD = FMath::ComputeBaryCentric2D(P, A, C, D);

		if (BaryACD.X >= 0 && BaryACD.Y >= 0 && BaryACD.Z >= 0)
		{
			return BaryACD.X;
		}

		return FVector::FReal(-1);
	}
}

void UPCGLandscapeSplineData::Initialize(ULandscapeSplinesComponent* InSplineComponent)
{
	check(InSplineComponent);
	Spline = InSplineComponent;
}

int UPCGLandscapeSplineData::GetNumSegments() const
{
	check(Spline);
	return Spline->GetSegments().Num();
}

float UPCGLandscapeSplineData::GetSegmentLength(int SegmentIndex) const
{
	check(Spline);
	check(SegmentIndex >= 0 && SegmentIndex < Spline->GetSegments().Num());
	
	const ULandscapeSplineSegment* Segment = Spline->GetSegments()[SegmentIndex];
	const TArray<FLandscapeSplineInterpPoint>& InterpPoints = Segment->GetPoints();
	float Length = 0;

	for (int PointIndex = 1; PointIndex < InterpPoints.Num(); ++PointIndex)
	{
		Length += (InterpPoints[PointIndex].Center - InterpPoints[PointIndex - 1].Center).Length();
	}

	return Length;
}

FVector UPCGLandscapeSplineData::GetLocationAtDistance(int SegmentIndex, float Distance) const
{
	check(Spline);
	check(SegmentIndex >= 0 && SegmentIndex < Spline->GetSegments().Num());

	const ULandscapeSplineSegment* Segment = Spline->GetSegments()[SegmentIndex];
	const TArray<FLandscapeSplineInterpPoint>& InterpPoints = Segment->GetPoints();
	float Length = Distance;

	for (int PointIndex = 1; PointIndex < InterpPoints.Num(); ++PointIndex)
	{
		const float SegmentLength = (InterpPoints[PointIndex].Center - InterpPoints[PointIndex - 1].Center).Length();
		if (SegmentLength > Length)
		{
			return InterpPoints[PointIndex - 1].Center + (InterpPoints[PointIndex].Center - InterpPoints[PointIndex - 1].Center) * (Length / SegmentLength);
		}
		else
		{
			Length -= SegmentLength;
		}
	}
	
	check(0);
	return FVector::Zero();
}

const UPCGPointData* UPCGLandscapeSplineData::CreatePointData(FPCGContext* Context) const
{
	check(Spline);
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGLandscapeSplineData::CreatePointData);

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGLandscapeSplineData*>(this));
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	// TODO: replace all the logic with sampling settings; currently it uses the landscape scaling as a basis
	const FTransform SplineTransform = Spline->GetComponentTransform();
	const FTransform LandscapeTransform = Spline->GetSplineOwner()->LandscapeActorToWorld();
	const FTransform SplineToLandscape = SplineTransform.GetRelativeTransform(LandscapeTransform);
	ULandscapeInfo* LandscapeInfo = Spline->GetSplineOwner()->GetLandscapeInfo();

	auto AddPoints = [&Points, &SplineTransform, &LandscapeTransform, &SplineToLandscape, LandscapeInfo](const FVector& A, const FVector& B, const FVector& C, const FVector& D, bool bComputeDensity, bool bAddA, bool bAddB) {

		auto AddPoint = [&Points](const FVector& P, float Density) {
			FPCGPoint& Point = Points.Emplace_GetRef();
			Point.Transform = FTransform(P); // pass in segment vector as orientation?
			Point.Seed = PCGHelpers::ComputeSeed((int)P.X, (int)P.Y, (int)P.Z);
			Point.Density = Density;
		};

		if (bAddA)
		{
			AddPoint(SplineTransform.TransformPosition(A), 1.0f);
		}

		// Interpolate points in the ABCD quad based on the given scales, computing density based on ratio from the A->B segment if the bComputeDensity flag is on, and 1.0f otherwise
		if (LandscapeInfo && LandscapeInfo->LandscapeActor.IsValid())
		{
			FBox QuadBoxOnLandscape(EForceInit::ForceInit);
			QuadBoxOnLandscape += SplineToLandscape.TransformPosition(A);
			QuadBoxOnLandscape += SplineToLandscape.TransformPosition(B);
			QuadBoxOnLandscape += SplineToLandscape.TransformPosition(C);
			QuadBoxOnLandscape += SplineToLandscape.TransformPosition(D);

			int32 MinX = FMath::CeilToInt(QuadBoxOnLandscape.Min.X);
			int32 MinY = FMath::CeilToInt(QuadBoxOnLandscape.Min.Y);
			int32 MaxX = FMath::FloorToInt(QuadBoxOnLandscape.Max.X);
			int32 MaxY = FMath::FloorToInt(QuadBoxOnLandscape.Max.Y);

			const FVector QuadNormal = (C - B).Cross(B - A).GetSafeNormal();

			for (int X = MinX; X <= MaxX; ++X)
			{
				for (int Y = MinY; Y <= MaxY; ++Y)
				{
					FVector TentativeLocation = LandscapeTransform.TransformPosition(FVector(X, Y, 0));
					const FVector TentativeLocationInSplineSpace = SplineTransform.InverseTransformPosition(TentativeLocation);

					const FVector::FReal ComputedDensity = PCGLandscapeDataHelpers::GetDensityInQuad(A, B, C, D, TentativeLocationInSplineSpace);

					// Check if the point would be in the quad
					if (ComputedDensity >= 0)
					{
						// TODO: note that we can't call GetHeight on the ULandscapeHEightfieldCollisionComponent, as it is not exported
						// so we have to resort to calling the method from the landscape actor
						TOptional<float> HeightAtVertex = LandscapeInfo->LandscapeActor->GetHeightAtLocation(TentativeLocation);
						if (HeightAtVertex.IsSet())
						{
							TentativeLocation.Z = HeightAtVertex.GetValue();
							AddPoint(TentativeLocation, bComputeDensity ? ComputedDensity : 1.0f);
						}
					}
				}
			}
		}

		if (bAddB)
		{
			AddPoint(SplineTransform.TransformPosition(B), 1.0f);
		}
	};
	
	const TArray<TObjectPtr<ULandscapeSplineSegment>>& Segments = Spline->GetSegments();

	// For each segment on the spline,
	for (int SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		// Sample points on and between points
		const TArray<FLandscapeSplineInterpPoint>& InterpPoints = Segments[SegmentIndex]->GetPoints();
		bool bIsLastSegment = (SegmentIndex == Segments.Num() - 1);

		for (int PointIndex = 1; PointIndex < InterpPoints.Num(); ++PointIndex)
		{
			bool bIsLastPointInSpline = (bIsLastSegment && PointIndex == InterpPoints.Num() - 1);

			const FLandscapeSplineInterpPoint& Start = InterpPoints[PointIndex - 1];
			const FLandscapeSplineInterpPoint& End = InterpPoints[PointIndex];

			AddPoints(Start.Center, End.Center, End.Left, Start.Left, /*bComputeDensity=*/false, /*bAddA=*/true, /*bAddB=*/false);
			AddPoints(Start.Left, End.Left, End.FalloffLeft, Start.FalloffLeft, /*bComputeDensity=*/true, /*bAddA=*/true, /*bAddB=*/bIsLastPointInSpline);
			AddPoints(End.Center, Start.Center, Start.Right, End.Right, /*bComputeDensity=*/false, /*bAddA=*/bIsLastPointInSpline, /*bAddB=*/false);
			AddPoints(End.Right, Start.Right, Start.FalloffRight, End.FalloffRight, /*bComputeDensity=*/true, /*bAddA=*/bIsLastPointInSpline, /*bAddB=*/true);
		}
	}

	UE_LOG(LogPCG, Verbose, TEXT("Landscape spline %s generated %d points on %d segments"), *Spline->GetFName().ToString(), Points.Num(), Segments.Num());

	return Data;
}

FBox UPCGLandscapeSplineData::GetBounds() const
{
	check(Spline);

	FBox Bounds(EForceInit::ForceInit);
	for (const TObjectPtr<ULandscapeSplineSegment>& Segment : Spline->GetSegments())
	{
		Bounds += Segment->GetBounds();
	}

	return Bounds;
}

bool UPCGLandscapeSplineData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	// TODO : add metadata support on poly lines
	// TODO : add support for bounds
	check(Spline);

	OutPoint.Transform = InTransform;
	OutPoint.SetLocalBounds(InBounds); // TODO: should maybe do Min.Z = Max.Z = 0 ?

	const FVector Position = Spline->GetComponentTransform().InverseTransformPosition(OutPoint.Transform.GetLocation());

	float PointDensity = 0.0f;

	for(const TObjectPtr<ULandscapeSplineSegment> Segment : Spline->GetSegments())
	{
		// Considering the landscape spline always exists on the landscape,
		// we'll ignore the Z component of the input here for the bounds check.
		if(!PCGHelpers::IsInsideBoundsXY(Segment->GetBounds(), Position))
		{
			continue;
		}
		
		const TArray<FLandscapeSplineInterpPoint>& InterpPoints = Segment->GetPoints();

		float SegmentDensity = 0.0f;

		for (int PointIndex = 1; PointIndex < InterpPoints.Num(); ++PointIndex)
		{
			const FLandscapeSplineInterpPoint& Start = InterpPoints[PointIndex - 1];
			const FLandscapeSplineInterpPoint& End = InterpPoints[PointIndex];

			float Density = 0.0f;

			// Note: these checks have no prior information on the structure of the data, except that they form quads.
			// Considering that the points on a given control point are probably aligned, we could do an early check
			// in the original quad (start left falloff -> start right falloff -> end right falloff -> end left falloff)
			// TODO: this sequence here can be optimized knowing that some checks will be redundant.
			Density = FMath::Max(Density, PCGLandscapeDataHelpers::GetDensityInQuad(Start.Center, End.Center, End.Left, Start.Left, Position) >= 0 ? 1.0f : 0.0f);
			Density = FMath::Max(Density, PCGLandscapeDataHelpers::GetDensityInQuad(Start.Left, End.Left, End.FalloffLeft, Start.FalloffLeft, Position));
			Density = FMath::Max(Density, PCGLandscapeDataHelpers::GetDensityInQuad(Start.Center, Start.Right, End.Right, End.Center, Position) >= 0 ? 1.0f : 0.0f);
			Density = FMath::Max(Density, PCGLandscapeDataHelpers::GetDensityInQuad(Start.Right, Start.FalloffRight, End.FalloffRight, End.Right, Position));

			if (Density > SegmentDensity)
			{
				SegmentDensity = Density;
			}
		}

		if (SegmentDensity > PointDensity)
		{
			PointDensity = SegmentDensity;
		}
	}

	OutPoint.Density = PointDensity;
	return OutPoint.Density > 0;
}