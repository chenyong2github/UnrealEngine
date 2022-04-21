// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGIntersectionData.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "PCGHelpers.h"
#include "Metadata/PCGMetadataAccessor.h"

namespace PCGIntersectionDataMaths
{
	float ComputeDensity(float InDensityA, float InDensityB, EPCGIntersectionDensityFunction InDensityFunction)
	{
		if (InDensityFunction == EPCGIntersectionDensityFunction::Minimum)
		{
			return FMath::Min(InDensityA, InDensityB);
		}
		else // default: Multiply
		{
			return InDensityA * InDensityB;
		}
	}
}

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
	if(!PCGHelpers::IsInsideBounds(CachedBounds, InPosition))
	{
		return 0;
	}
	else if(PCGHelpers::IsInsideBounds(CachedStrictBounds, InPosition))
	{
		return 1.0f;
	}
	else
	{
		float Density = A->GetDensityAtPosition(InPosition);
		if (Density > 0)
		{
			Density = PCGIntersectionDataMaths::ComputeDensity(Density, B->GetDensityAtPosition(InPosition), DensityFunction);
		}

		return Density;
	}
}

FVector UPCGIntersectionData::TransformPosition(const FVector& InPosition) const
{
	check(A && B);
	return A->HasNonTrivialTransform() ? A->TransformPosition(InPosition) : B->TransformPosition(InPosition);
}

FPCGPoint UPCGIntersectionData::TransformPoint(const FPCGPoint& InPoint) const
{ 
	check(A && B);
	const UPCGSpatialData* X = A->HasNonTrivialTransform() ? A : B;
	const UPCGSpatialData* Y = (X == A) ? B : A;

	FPCGPoint TransformedPoint = X->TransformPoint(InPoint);
	if (TransformedPoint.Density > 0)
	{
		TransformedPoint.Density = PCGIntersectionDataMaths::ComputeDensity(TransformedPoint.Density, Y->GetDensityAtPosition(TransformedPoint.Transform.GetLocation()), DensityFunction);
	}	

	return TransformedPoint;
}

bool UPCGIntersectionData::GetPointAtPosition(const FVector& InPosition, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	check(A && B);
	const UPCGSpatialData* X = A->HasNonTrivialTransform() ? A : B;
	const UPCGSpatialData* Y = (X == A) ? B : A;

	FPCGPoint PointFromX;
	if (!X->GetPointAtPosition(InPosition, PointFromX, OutMetadata))
	{
		return false;
	}

	FPCGPoint PointFromY;
	if (!Y->GetPointAtPosition(PointFromX.Transform.GetLocation(), PointFromY, OutMetadata))
	{
		return false;
	}

	// Merge points into a single point
	OutPoint = PointFromX;
	OutPoint.Density = PCGIntersectionDataMaths::ComputeDensity(PointFromX.Density, PointFromY.Density, DensityFunction);
	OutPoint.Color = PointFromX.Color * PointFromY.Color;

	if (OutMetadata)
	{
		OutMetadata->MergePointAttributes(PointFromX, PointFromY, OutPoint, EPCGMetadataOp::Min);
	}

	return true;
}

bool UPCGIntersectionData::HasNonTrivialTransform() const
{
	check(A && B);
	return A->HasNonTrivialTransform() || B->HasNonTrivialTransform();
}

const UPCGPointData* UPCGIntersectionData::CreatePointData(FPCGContext* Context) const
{
	check(A && B);
	// TODO: this is a placeholder;
	// Here we will get the point data from the lower-dimensionality data
	// and then cull out any of the points that are outside the bounds of the other
	if (A->GetDimension() <= B->GetDimension())
	{
		return CreateAndFilterPointData(Context, A, B);
	}
	else
	{
		return CreateAndFilterPointData(Context, B, A);
	}
}

UPCGPointData* UPCGIntersectionData::CreateAndFilterPointData(FPCGContext* Context, const UPCGSpatialData* X, const UPCGSpatialData* Y) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGIntersectionData::CreateAndFilterPointData);
	check(X && Y);
	check(X->GetDimension() <= Y->GetDimension());

	const UPCGPointData* SourcePointData = X->ToPointData(Context);

	if (!SourcePointData)
	{
		UE_LOG(LogPCG, Error, TEXT("Intersection unable to get source points"));
		return nullptr;
	}

	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGIntersectionData*>(this));
	Data->InitializeFromData(this, SourcePointData->Metadata);
	Data->Metadata->AddAttributes(Y->Metadata);

	TArray<FPCGPoint>& TargetPoints = Data->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(Context, SourcePoints.Num(), TargetPoints, [this, Data, SourcePointData, &SourcePoints, Y](int32 Index, FPCGPoint& OutPoint)
	{
		const FPCGPoint& Point = SourcePoints[Index];

		FPCGPoint PointFromY;
#if WITH_EDITORONLY_DATA
		if (!Y->GetPointAtPosition(Point.Transform.GetLocation(), PointFromY, Data->Metadata) && !bKeepZeroDensityPoints)
#else
		if (!Y->GetPointAtPosition(Point.Transform.GetLocation(), PointFromY, Data->Metadata))
#endif
		{
			return false;
		}

		OutPoint = Point;
		UPCGMetadataAccessorHelpers::InitializeMetadata(OutPoint, Data->Metadata, Point);
		OutPoint.Density = PCGIntersectionDataMaths::ComputeDensity(Point.Density, PointFromY.Density, DensityFunction);
		OutPoint.Color = Point.Color * PointFromY.Color;

		if (Data->Metadata)
		{
			Data->Metadata->MergePointAttributes(Point, SourcePointData->Metadata, PointFromY, Data->Metadata, OutPoint, EPCGMetadataOp::Min);
		}

		return true;
	});

	UE_LOG(LogPCG, Verbose, TEXT("Intersection generated %d points from %d source points"), TargetPoints.Num(), SourcePoints.Num());

	return Data;
}