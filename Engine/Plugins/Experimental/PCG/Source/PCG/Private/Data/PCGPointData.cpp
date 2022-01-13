// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGPointData.h"

#include "PCGHelpers.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopeLock.h"

FPCGPointRef::FPCGPointRef(const FPCGPoint& InPoint)
{
	Point = &InPoint;
	Bounds = InPoint.GetBounds();
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
		FBoxSphereBounds PointBounds = Point.GetBounds();
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
	if (const FPCGPoint* BestPoint = GetPointAtPosition(InPosition))
	{
		// It is going to be hard to do a linear fall-off here and maybe not really required.
		return BestPoint->Density;
	}
	else
	{
		return 0;
	}
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