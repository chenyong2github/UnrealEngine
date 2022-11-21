// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSplineData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSurfaceData.h"
#include "Elements/PCGSplineSampler.h"

#include "Components/SplineComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSplineData)

void UPCGSplineData::Initialize(USplineComponent* InSpline)
{
	check(InSpline);
	Spline = InSpline;
	TargetActor = InSpline->GetOwner();

	CachedBounds = PCGHelpers::GetActorBounds(Spline->GetOwner());

	// Expand bounds by the radius of points, otherwise sections of the curve that are close
	// to the bounds will report an invalid density.
	FVector SplinePointsRadius = FVector::ZeroVector;
	const FInterpCurveVector& SplineScales = Spline->GetSplinePointsScale();
	for (const FInterpCurvePoint<FVector>& SplineScale : SplineScales.Points)
	{
		SplinePointsRadius = FVector::Max(SplinePointsRadius, SplineScale.OutVal.GetAbs());
	}

	CachedBounds = CachedBounds.ExpandBy(SplinePointsRadius, SplinePointsRadius);
}

int UPCGSplineData::GetNumSegments() const
{
	return Spline ? Spline->GetNumberOfSplineSegments() : 0;
}

FVector::FReal UPCGSplineData::GetSegmentLength(int SegmentIndex) const
{
	return Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex + 1) - Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex);
}

FVector UPCGSplineData::GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	return Spline->GetLocationAtDistanceAlongSpline(Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) + Distance, ESplineCoordinateSpace::World);
}

FTransform UPCGSplineData::GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, FBox* OutBounds) const
{
	if (OutBounds)
	{
		*OutBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector);
	}

	return Spline->GetTransformAtDistanceAlongSpline(Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) + Distance, ESplineCoordinateSpace::World, /*bUseScale=*/true);
}

const UPCGPointData* UPCGSplineData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSplineData::CreatePointData);
	UPCGPointData* Data = NewObject<UPCGPointData>();
	Data->InitializeFromData(this);

	FPCGSplineSamplerParams SamplerParams;
	SamplerParams.Mode = EPCGSplineSamplingMode::Distance;

	PCGSplineSampler::SampleLineData(this, this, SamplerParams, Data);

	UE_LOG(LogPCG, Verbose, TEXT("Spline %s generated %d points"), *Spline->GetFName().ToString(), Data->GetPoints().Num());

	return Data;
}

FBox UPCGSplineData::GetBounds() const
{
	return CachedBounds;
}

bool UPCGSplineData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	// TODO: support metadata
	// TODO: support proper bounds

	// Find nearest point on spline
	const FVector InPosition = InTransform.GetLocation();
	float NearestPointKey = Spline->FindInputKeyClosestToWorldLocation(InPosition);
	FTransform NearestTransform = Spline->GetTransformAtSplineInputKey(NearestPointKey, ESplineCoordinateSpace::World, true);
	FVector LocalPoint = NearestTransform.InverseTransformPosition(InPosition);
	
	// Linear fall off based on the distance to the nearest point
	// TODO: should be based on explicit settings
	float Distance = LocalPoint.Length();
	if (Distance > 1.0f)
	{
		return false;
	}
	else
	{
		OutPoint.Transform = NearestTransform;
		OutPoint.Transform.SetLocation(InPosition);
		OutPoint.SetLocalBounds(InBounds);
		OutPoint.Density = 1.0f - Distance;

		return true;
	}
}

UPCGProjectionData* UPCGSplineData::ProjectOn(const UPCGSpatialData* InOther, const FPCGProjectionParams& InParams) const
{
	if(InOther->GetDimension() == 2)
	{
		UPCGSplineProjectionData* SplineProjectionData = NewObject<UPCGSplineProjectionData>();
		SplineProjectionData->Initialize(this, InOther, InParams);
		return SplineProjectionData;
	}
	else
	{
		return Super::ProjectOn(InOther, InParams);
	}
}

UPCGSpatialData* UPCGSplineData::CopyInternal() const
{
	UPCGSplineData* NewSplineData = NewObject<UPCGSplineData>();

	NewSplineData->Spline = Spline;
	NewSplineData->CachedBounds = CachedBounds;

	return NewSplineData;
}

FVector2D UPCGSplineProjectionData::Project(const FVector& InVector) const
{
	const FVector& SurfaceNormal = GetSurface()->GetNormal();
	FVector Projection = InVector - InVector.ProjectOnToNormal(SurfaceNormal);

	// Ignore smallest absolute coordinate value.
	// Normally, one should be zero, but because of numerical precision,
	// We'll make sure we're doing it right
	FVector::FReal SmallestCoordinate = TNumericLimits<FVector::FReal>::Max();
	int SmallestCoordinateAxis = -1;

	for (int Axis = 0; Axis < 3; ++Axis)
	{
		FVector::FReal AbsoluteCoordinateValue = FMath::Abs(Projection[Axis]);
		if (AbsoluteCoordinateValue < SmallestCoordinate)
		{
			SmallestCoordinate = AbsoluteCoordinateValue;
			SmallestCoordinateAxis = Axis;
		}
	}

	FVector2D Projection2D;
	int AxisIndex = 0;
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		if (Axis != SmallestCoordinateAxis)
		{
			Projection2D[AxisIndex++] = Projection[Axis];
		}
	}

	return Projection2D;
}

void UPCGSplineProjectionData::Initialize(const UPCGSplineData* InSourceSpline, const UPCGSpatialData* InTargetSurface, const FPCGProjectionParams& InParams)
{
	Super::Initialize(InSourceSpline, InTargetSurface, InParams);

	const USplineComponent* Spline = GetSpline()->Spline.Get();
	const FVector& SurfaceNormal = GetSurface()->GetNormal();

	if (Spline)
	{
		const FInterpCurveVector& SplinePosition = Spline->GetSplinePointsPosition();

		// Build projected spline data
		ProjectedPosition.bIsLooped = SplinePosition.bIsLooped;
		ProjectedPosition.LoopKeyOffset = SplinePosition.LoopKeyOffset;

		ProjectedPosition.Points.Reserve(SplinePosition.Points.Num());

		for (const FInterpCurvePoint<FVector>& SplinePoint : SplinePosition.Points)
		{
			FInterpCurvePoint<FVector2D>& ProjectedPoint = ProjectedPosition.Points.Emplace_GetRef();

			ProjectedPoint.InVal = SplinePoint.InVal;
			ProjectedPoint.OutVal = Project(SplinePoint.OutVal);
			// TODO: correct tangent if it becomes null
			ProjectedPoint.ArriveTangent = Project(SplinePoint.ArriveTangent).GetSafeNormal();
			ProjectedPoint.LeaveTangent = Project(SplinePoint.LeaveTangent).GetSafeNormal();
			ProjectedPoint.InterpMode = SplinePoint.InterpMode;
		}
	}
}

const UPCGSplineData* UPCGSplineProjectionData::GetSpline() const
{
	return Cast<const UPCGSplineData>(Source);
}

const UPCGSpatialData* UPCGSplineProjectionData::GetSurface() const
{
	return Target;
}

UPCGSpatialData* UPCGSplineProjectionData::CopyInternal() const
{
	UPCGSplineProjectionData* NewProjectionData = NewObject<UPCGSplineProjectionData>();

	CopyBaseProjectionClass(NewProjectionData);

	NewProjectionData->ProjectedPosition = ProjectedPosition;

	return NewProjectionData;
}
