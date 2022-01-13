// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGIntersectionData.h"
#include "Data/PCGPointData.h"

void UPCGIntersectionData::Initialize(const UPCGSpatialData* InA, const UPCGSpatialData* InB)
{
	check(InA && InB);
	A = InA;
	B = InB;
	TargetActor = A->TargetActor;

	CachedBounds = A->GetBounds().Overlap(B->GetBounds());
	CachedStrictBounds = A->GetStrictBounds().Overlap(B->GetStrictBounds());
}

int UPCGIntersectionData::GetDimension() const
{
	check(A && B);
	return FMath::Min(A->GetDimension(), B->GetDimension());
}

FBox UPCGIntersectionData::GetBounds() const
{
	check(A && B);
	return CachedBounds;
}

FBox UPCGIntersectionData::GetStrictBounds() const
{
	check(A && B);
	return CachedStrictBounds;
}

float UPCGIntersectionData::GetDensityAtPosition(const FVector& InPosition) const
{
	check(A && B);
	if (!CachedBounds.IsInside(InPosition))
	{
		return 0;
	}
	else if (CachedStrictBounds.IsInside(InPosition))
	{
		return 1.0f;
	}
	else
	{
		return A->GetDensityAtPosition(InPosition) * B->GetDensityAtPosition(InPosition);
	}
}

const UPCGPointData* UPCGIntersectionData::CreatePointData() const
{
	check(A && B);
	// TODO: this is a placeholder;
	// Here we will get the point data from the lower-dimensionality data
	// and then cull out any of the points that are outside the bounds of the other
	if (A->GetDimension() <= B->GetDimension())
	{
		return CreateAndFilterPointData(A, B);
	}
	else
	{
		return CreateAndFilterPointData(B, A);
	}
}

UPCGPointData* UPCGIntersectionData::CreateAndFilterPointData(const UPCGSpatialData* X, const UPCGSpatialData* Y) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGIntersectionData::CreateAndFilterPointData);
	check(X && Y);
	check(X->GetDimension() <= Y->GetDimension());

	const UPCGPointData* SourcePointData = X->ToPointData();

	if (!SourcePointData)
	{
		return nullptr;
	}

	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGIntersectionData*>(this));
	Data->TargetActor = TargetActor;
	TArray<FPCGPoint>& TargetPoints = Data->GetMutablePoints();

	// TODO: go through only the points in the bounding box, otherwise we know beforehand
	// that the density will be zero. Requires that points are stored in a better data structure.
	for (const FPCGPoint& Point : SourcePoints)
	{
		const float YDensity = Y->GetDensityAtPosition(Point.Transform.GetLocation());
		if (YDensity > 0)
		{
			FPCGPoint& NewPoint = TargetPoints.Add_GetRef(Point);
			NewPoint.Density *= YDensity;
		}
	}

	return Data;
}