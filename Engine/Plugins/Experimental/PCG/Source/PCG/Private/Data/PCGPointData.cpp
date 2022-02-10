// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGPointData.h"

#include "PCGHelpers.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopeLock.h"

namespace PCGPointHelpers
{
	float ManhattanDensity(const FPCGPoint& InPoint, const FVector& InPosition)
	{
		FVector LocalPosition = InPoint.Transform.InverseTransformPosition(InPosition);
		LocalPosition /= InPoint.Extents;

		// ]-2+s, 2-s] is the valid range of values
		const FVector::FReal LowerBound = InPoint.Steepness - 2;
		const FVector::FReal HigherBound = 2 - InPoint.Steepness;
		
		if (LocalPosition.X <= LowerBound || LocalPosition.X > HigherBound ||
			LocalPosition.Y <= LowerBound || LocalPosition.Y > HigherBound ||
			LocalPosition.Z <= LowerBound || LocalPosition.Z > HigherBound)
		{
			return 0;
		}

		// [-s, +s] is the range where the density is 1 on that axis
		const FVector::FReal XDist = FMath::Max(0, FMath::Abs(LocalPosition.X) - InPoint.Steepness);
		const FVector::FReal YDist = FMath::Max(0, FMath::Abs(LocalPosition.Y) - InPoint.Steepness);
		const FVector::FReal ZDist = FMath::Max(0, FMath::Abs(LocalPosition.Z) - InPoint.Steepness);

		const FVector::FReal DistanceScale = FMath::Max(2 - 2 * InPoint.Steepness, KINDA_SMALL_NUMBER);

		//Note: for euclidean, we could do 1 - (dist / scale)^2
		//Note: for maximum norm, we could do density * max(x factor, y factor, z factor)
		return InPoint.Density * (1 - XDist/DistanceScale) * (1 - YDist/DistanceScale) * (1 - ZDist/DistanceScale);
	}
}

FPCGPointRef::FPCGPointRef(const FPCGPoint& InPoint)
{
	Point = &InPoint;
	Bounds = InPoint.GetDensityBounds();
}

FPCGPointRef::FPCGPointRef(const FPCGPointRef& InPointRef)
{
	Point = InPointRef.Point;
	Bounds = InPointRef.Bounds;
}

TArray<FPCGPoint>& UPCGPointData::GetMutablePoints()
{
	bOctreeIsDirty = true;
	bBoundsAreDirty = true;
	return Points;
}

const UPCGPointData::PointOctree& UPCGPointData::GetOctree() const
{
	if (bOctreeIsDirty)
	{
		RebuildOctree();
	}

	return Octree;
}

FBox UPCGPointData::GetBounds() const
{
	if (bBoundsAreDirty)
	{
		RecomputeBounds();
	}

	return Bounds;
}

void UPCGPointData::RecomputeBounds() const
{
	FScopeLock Lock(&CachedDataLock);

	if (!bBoundsAreDirty)
	{
		return;
	}

	FBox NewBounds(EForceInit::ForceInit);
	for (const FPCGPoint& Point : Points)
	{
		FBoxSphereBounds PointBounds = Point.GetDensityBounds();
		NewBounds += FBox::BuildAABB(PointBounds.Origin, PointBounds.BoxExtent);
	}

	Bounds = NewBounds;
	bBoundsAreDirty = false;
}

void UPCGPointData::CopyPointsFrom(const UPCGPointData* InData, const TArray<int>& InDataIndices)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPointData::CopyPointsFrom);
	check(InData);
	Points.SetNum(InDataIndices.Num());

	// TODO: parallel-for this?
	for (int PointIndex = 0; PointIndex < InDataIndices.Num(); ++PointIndex)
	{
		Points[PointIndex] = InData->Points[InDataIndices[PointIndex]];
	}

	bBoundsAreDirty = true;
	bOctreeIsDirty = true;
}

void UPCGPointData::SetPoints(const TArray<FPCGPoint>& InPoints)
{
	GetMutablePoints() = InPoints;
}

void UPCGPointData::Initialize(AActor* InActor)
{
	check(InActor);

	Points.SetNum(1);
	Points[0].Transform = InActor->GetActorTransform();

	const FVector& Position = Points[0].Transform.GetLocation();
	Points[0].Seed = PCGHelpers::ComputeSeed((int)Position.X, (int)Position.Y, (int)Position.Z);

	TargetActor = InActor;
}

const FPCGPoint* UPCGPointData::GetPointAtPosition(const FVector& InPosition) const
{
	if (bOctreeIsDirty)
	{
		RebuildOctree();
	}

	const FPCGPoint* BestPoint = nullptr;

	Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(InPosition, FVector::Zero()), [&BestPoint](const FPCGPointRef& InPointRef) {
		if (!BestPoint || BestPoint->Density < InPointRef.Point->Density)
		{
			BestPoint = InPointRef.Point;
		}
	});

	return BestPoint;
}

float UPCGPointData::GetDensityAtPosition(const FVector& InPosition) const
{
	if (bOctreeIsDirty)
	{
		RebuildOctree();
	}

	float Density = 0;

	Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(InPosition, FVector::Zero()), [&InPosition, &Density](const FPCGPointRef& InPointRef) {
		Density += PCGPointHelpers::ManhattanDensity(*InPointRef.Point, InPosition);
	});

	return FMath::Min(Density, 1.0f);
}

void UPCGPointData::RebuildOctree() const
{
	FScopeLock Lock(&CachedDataLock);

	if (!bOctreeIsDirty)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPointData::RebuildOctree)
	check(bOctreeIsDirty);

	FBox PointBounds = GetBounds();
	TOctree2<FPCGPointRef, FPCGPointRefSemantics> NewOctree(PointBounds.GetCenter(), PointBounds.GetExtent().Length());

	for (const FPCGPoint& Point : Points)
	{
		NewOctree.AddElement(FPCGPointRef(Point));
	}

	Octree = NewOctree;
	bOctreeIsDirty = false;
}