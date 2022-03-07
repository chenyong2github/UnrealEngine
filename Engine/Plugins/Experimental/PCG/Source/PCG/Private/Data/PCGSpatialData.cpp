// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSpatialData.h"
#include "Data/PCGDifferenceData.h"
#include "Data/PCGIntersectionData.h"
#include "Data/PCGProjectionData.h"
#include "Data/PCGUnionData.h"

const UPCGPointData* UPCGSpatialDataWithPointCache::ToPointData(FPCGContext* Context) const
{
	if (!CachedPointData)
	{
		CacheLock.Lock();

		if (!CachedPointData)
		{
			CachedPointData = CreatePointData(Context);
		}

		CacheLock.Unlock();
	}

	return CachedPointData;
}

FPCGPoint UPCGSpatialData::TransformPoint(const FPCGPoint& InPoint) const
{
	FPCGPoint TransformedPoint = InPoint;
	TransformedPoint.Transform.SetTranslation(TransformPosition(InPoint.Transform.GetLocation()));
	TransformedPoint.Density *= GetDensityAtPosition(InPoint.Transform.GetLocation());
	return TransformedPoint;
}

UPCGIntersectionData* UPCGSpatialData::IntersectWith(const UPCGSpatialData* InOther) const
{
	UPCGIntersectionData* IntersectionData = NewObject<UPCGIntersectionData>(const_cast<UPCGSpatialData*>(this));
	IntersectionData->Initialize(this, InOther);

	return IntersectionData;
}

UPCGProjectionData* UPCGSpatialData::ProjectOn(const UPCGSpatialData* InOther) const
{
	UPCGProjectionData* ProjectionData = NewObject<UPCGProjectionData>(const_cast<UPCGSpatialData*>(this));
	ProjectionData->Initialize(this, InOther);

	return ProjectionData;
}

UPCGUnionData* UPCGSpatialData::UnionWith(const UPCGSpatialData* InOther) const
{
	UPCGUnionData* UnionData = NewObject<UPCGUnionData>(const_cast<UPCGSpatialData*>(this));
	UnionData->Initialize(this, InOther);

	return UnionData;
}

UPCGDifferenceData* UPCGSpatialData::Subtract(const UPCGSpatialData* InOther) const
{
	UPCGDifferenceData* DifferenceData = NewObject<UPCGDifferenceData>(const_cast<UPCGSpatialData*>(this));
	DifferenceData->Initialize(this);
	DifferenceData->AddDifference(InOther);

	return DifferenceData;
}