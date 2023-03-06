// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSplineData.h"

#include "Data/PCGPolyLineData.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGProjectionData.h"
#include "Elements/PCGSplineSampler.h"

#include "Components/SplineComponent.h"
#include "Data/PCGSpatialData.h"
#include "Serialization/ArchiveCrc32.h"

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

void UPCGSplineData::AddToCrc(FArchiveCrc32& Ar) const
{
	// TODO: do not rely on having a live spline if we want to CRC this.
	Super::AddToCrc(Ar);

	//check(Spline);

	//Ar << const_cast<FBox&>(CachedBounds);

	//Ar << const_cast<FInterpCurveVector&>(Spline->GetSplinePointsPosition());
	//Ar << const_cast<FInterpCurveQuat&>(Spline->GetSplinePointsRotation());
	//Ar << const_cast<FInterpCurveVector&>(Spline->GetSplinePointsScale());

	//Ar << const_cast<FTransform&>(Spline->GetComponentTransform());

	//bool bIsClosed = Spline->IsClosedLoop();
	//Ar << bIsClosed;
}

FTransform UPCGSplineData::GetTransform() const
{
	return Spline ? Spline->GetComponentTransform() : FTransform::Identity;
}

int UPCGSplineData::GetNumSegments() const
{
	return Spline ? Spline->GetNumberOfSplineSegments() : 0;
}

FVector::FReal UPCGSplineData::GetSegmentLength(int SegmentIndex) const
{
	return Spline ? Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex + 1) - Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) : 0;
}

FVector UPCGSplineData::GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace) const
{
	return Spline ? Spline->GetLocationAtDistanceAlongSpline(Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) + Distance, bWorldSpace ? ESplineCoordinateSpace::World : ESplineCoordinateSpace::Local) : FVector::ZeroVector;
}

FTransform UPCGSplineData::GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace, FBox* OutBounds) const
{
	if (OutBounds)
	{
		*OutBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector);
	}

	return Spline ? Spline->GetTransformAtDistanceAlongSpline(Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) + Distance, bWorldSpace ? ESplineCoordinateSpace::World : ESplineCoordinateSpace::Local, /*bUseScale=*/true) : FTransform::Identity;
}

FVector::FReal UPCGSplineData::GetCurvatureAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	if (!Spline)
	{
		return 0;
	}

	const float FullDistance = Spline->GetDistanceAlongSplineAtSplinePoint(SegmentIndex) + Distance;
	const float Param = Spline->SplineCurves.ReparamTable.Eval(FullDistance, 0.0f);

	// Since we need the first derivative (e.g. very similar to direction) to have its norm, we'll get the value directly
	const FVector FirstDerivative = Spline->SplineCurves.Position.EvalDerivative(Param, FVector::ZeroVector);
	const FVector::FReal FirstDerivativeLength = FMath::Max(FirstDerivative.Length(), UE_DOUBLE_SMALL_NUMBER);
	const FVector ForwardVector = FirstDerivative / FirstDerivativeLength;
	const FVector SecondDerivative = Spline->SplineCurves.Position.EvalSecondDerivative(Param, FVector::ZeroVector);
	// Orthogonalize the second derivative and obtain the curvature vector
	const FVector CurvatureVector = SecondDerivative - (SecondDerivative | ForwardVector) * ForwardVector;
	
	// Finally, the curvature is the ratio of the norms of the curvature vector over the first derivative norm
	const FVector::FReal Curvature = CurvatureVector.Length() / FirstDerivativeLength;

	// Compute sign based on sign of curvature vs. right axis
	const FVector RightVector = Spline->GetRightVectorAtSplineInputKey(Param, ESplineCoordinateSpace::Local);
	return FMath::Sign(RightVector | CurvatureVector) * Curvature;
}

const UPCGPointData* UPCGSplineData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSplineData::CreatePointData);
	UPCGPointData* Data = NewObject<UPCGPointData>();
	Data->InitializeFromData(this);

	FPCGSplineSamplerParams SamplerParams;
	SamplerParams.Mode = EPCGSplineSamplingMode::Distance;

	PCGSplineSampler::SampleLineData(this, this, nullptr, SamplerParams, Data);

	if (Spline)
	{
		UE_LOG(LogPCG, Verbose, TEXT("Spline %s generated %d points"), *Spline->GetFName().ToString(), Data->GetPoints().Num());
	}

	return Data;
}

FBox UPCGSplineData::GetBounds() const
{
	return CachedBounds;
}

bool UPCGSplineData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	if (!Spline)
	{
		return false;
	}

	// TODO: support metadata
	// TODO: support proper bounds

	// This is a pure SamplePoint implementation.
	
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

UPCGSpatialData* UPCGSplineData::ProjectOn(const UPCGSpatialData* InOther, const FPCGProjectionParams& InParams) const
{
	if (InOther->GetDimension() == 2)
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

bool UPCGSplineProjectionData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	// TODO: support metadata - we don't currently have a good representation of what metadata entries mean for non-point data
	// TODO: use InBounds when sampling spline (sample in area rather than at closest point)

	if (!ProjectionParams.bProjectPositions)
	{
		// If we're not moving anything around, then just defer to super which will sample 3D spline, to make SamplePoint() consistent with behaviour
		// on 'concrete' data (points).
		return Super::SamplePoint(InTransform, InBounds, OutPoint, OutMetadata);
	}

	check(GetSpline());
	if (!GetSpline()->Spline)
	{
		return false;
	}

	// Find nearest point on projected spline by lifting point along projection direction to closest position on spline. This way
	// when we sample the spline we get a similar result to if the spline had been projected onto the surface.

	const FVector InPosition = InTransform.GetLocation();
	const USplineComponent* Spline = GetSpline()->Spline.Get();
	check(GetSurface());
	const FVector& SurfaceNormal = GetSurface()->GetNormal();

	// Project to 2D space
	const FTransform LocalTransform = InTransform * Spline->GetComponentTransform().Inverse();
	FVector2D LocalPosition2D = Project(LocalTransform.GetLocation());
	float Dummy;
	// Find nearest key on 2D spline
	float NearestInputKey = ProjectedPosition.FindNearest(LocalPosition2D, Dummy);
	// TODO: if we didn't want to hand off density computation to the spline and do it here instead, we could do it in 2D space.
	// Find point on original spline using the previously found key. Note this is an approximation that might not hold true since
	// we are changing the curve length. Also, to support surface orientations that are not axis aligned, the project function
	// probably needs to construct into a coordinate space and project onto it rather than discarding an axis, otherwise project
	// coordinates may be non-uniformly scaled.
	const FVector NearestPointOnSpline = Spline->GetLocationAtSplineInputKey(NearestInputKey, ESplineCoordinateSpace::World);
	const FVector PointOnLine = FMath::ClosestPointOnInfiniteLine(InPosition, InPosition + SurfaceNormal, NearestPointOnSpline);

	// TODO: this is super inefficient, could be done in 2D if we duplicate the sampling code
	FPCGPoint SplinePoint;
	if (GetSpline()->SamplePoint(FTransform(PointOnLine), InBounds, SplinePoint, OutMetadata))
	{
		FPCGPoint SurfacePoint;
		if (GetSurface()->SamplePoint(SplinePoint.Transform, InBounds, SurfacePoint, OutMetadata))
		{
			OutPoint = SplinePoint;

			ApplyProjectionResult(SurfacePoint, OutPoint);

			if (OutMetadata)
			{
				if (SplinePoint.MetadataEntry != PCGInvalidEntryKey && SurfacePoint.MetadataEntry != PCGInvalidEntryKey)
				{
					OutMetadata->MergePointAttributesSubset(SplinePoint, OutMetadata, GetSpline()->Metadata, SurfacePoint, OutMetadata, GetSurface()->Metadata, OutPoint, ProjectionParams.AttributeMergeOperation);
				}
				else if (SurfacePoint.MetadataEntry != PCGInvalidEntryKey)
				{
					OutPoint.MetadataEntry = SurfacePoint.MetadataEntry;
				}
			}

			return true;
		}
	}

	return false;
}

FVector2D UPCGSplineProjectionData::Project(const FVector& InVector) const
{
	check(GetSurface());
	const FVector& SurfaceNormal = GetSurface()->GetNormal();
	FVector Projection = InVector - InVector.ProjectOnToNormal(SurfaceNormal);

	// Find the largest coordinate of the normal and use as the projection axis
	int BiggestCoordinateAxis = 0;
	FVector::FReal BiggestCoordinate = FMath::Abs(SurfaceNormal[BiggestCoordinateAxis]);

	for (int Axis = 1; Axis < 3; ++Axis)
	{
		FVector::FReal AbsoluteCoordinateValue = FMath::Abs(SurfaceNormal[Axis]);
		if (AbsoluteCoordinateValue > BiggestCoordinate)
		{
			BiggestCoordinate = AbsoluteCoordinateValue;
			BiggestCoordinateAxis = Axis;
		}
	}

	// Discard the projection axis coordinate
	FVector2D Projection2D;
	int AxisIndex = 0;
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		if (Axis != BiggestCoordinateAxis)
		{
			Projection2D[AxisIndex++] = Projection[Axis];
		}
	}

	return Projection2D;
}

void UPCGSplineProjectionData::Initialize(const UPCGSplineData* InSourceSpline, const UPCGSpatialData* InTargetSurface, const FPCGProjectionParams& InParams)
{
	Super::Initialize(InSourceSpline, InTargetSurface, InParams);

	check(GetSpline());
	const USplineComponent* Spline = GetSpline()->Spline.Get();

	check(GetSurface());
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
