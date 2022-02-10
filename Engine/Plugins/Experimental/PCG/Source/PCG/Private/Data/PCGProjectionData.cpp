// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGProjectionData.h"

void UPCGProjectionData::Initialize(const UPCGSpatialData* InSource, const UPCGSpatialData* InTarget)
{
	check(InSource && InTarget);
	// TODO: improve support for higher-dimension projection.
	// The problem is that there isn't a valid 1:1 mapping otherwise
	check(InSource->GetDimension() <= InTarget->GetDimension());
	Source = InSource;
	Target = InTarget;
	TargetActor = InSource->TargetActor;

	CachedBounds = ProjectBounds(Source->GetBounds());
	CachedStrictBounds = ProjectBounds(Source->GetStrictBounds());
}

int UPCGProjectionData::GetDimension() const
{
	check(Source && Target);
	return FMath::Min(Source->GetDimension(), Target->GetDimension());
}

FBox UPCGProjectionData::GetBounds() const
{
	check(Source && Target);
	return CachedBounds;
}

FBox UPCGProjectionData::GetStrictBounds() const
{
	check(Source && Target);
	return CachedStrictBounds;
}

FVector UPCGProjectionData::GetNormal() const
{
	check(Source && Target);
	if (Source->GetDimension() > Target->GetDimension())
	{
		return Source->GetNormal();
	}
	else
	{
		return Target->GetNormal();
	}
}

FBox UPCGProjectionData::ProjectBounds(const FBox& InBounds) const
{
	FBox Bounds(EForceInit::ForceInit);

	for (int Corner = 0; Corner < 8; ++Corner)
	{
		Bounds += TransformPosition(
			FVector(
				(Corner / 4) ? InBounds.Max.X : InBounds.Min.X,
				((Corner / 2) % 2) ? InBounds.Max.Y : InBounds.Min.Y,
				(Corner % 2) ? InBounds.Max.Z : InBounds.Min.Z));
	}

	return Bounds;
}

float UPCGProjectionData::GetDensityAtPosition(const FVector& InPosition) const
{
	// TODO: improve projection/unprojection mechanism
	return Source->GetDensityAtPosition(InPosition);
}

FVector UPCGProjectionData::TransformPosition(const FVector& InPosition) const
{
	// TODO: improve projection/unprojection mechanism
	return Target->TransformPosition(InPosition);
}

FPCGPoint UPCGProjectionData::TransformPoint(const FPCGPoint& InPoint) const
{
	// TODO: improve projection/unprojection mechanism
	return Target->TransformPoint(InPoint);
}

bool UPCGProjectionData::HasNonTrivialTransform() const
{
	return Target->HasNonTrivialTransform();
}

const UPCGPointData* UPCGProjectionData::CreatePointData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGProjectionData::CreatePointData);
	// TODO: add mechanism in the ToPointData so we can pass in a transform
	// so we can forego creating the points twice if they're not used.
	const UPCGPointData* SourcePointData = Source->ToPointData();
	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();

	UPCGPointData* PointData = NewObject<UPCGPointData>(const_cast<UPCGProjectionData*>(this));
	PointData->TargetActor = TargetActor;
	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();

	Points.Reserve(SourcePoints.Num());
	for (const FPCGPoint& SourcePoint : SourcePoints)
	{
		FPCGPoint ProjectedPoint = TransformPoint(SourcePoint);
#if WITH_EDITORONLY_DATA
		if(ProjectedPoint.Density > 0 || bKeepZeroDensityPoints)
#else
		if (ProjectedPoint.Density > 0)
#endif
		{
			Points.Add(MoveTemp(ProjectedPoint));
		}
	}

	return PointData;
}