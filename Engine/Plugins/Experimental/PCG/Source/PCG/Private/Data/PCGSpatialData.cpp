// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSpatialData.h"
#include "Data/PCGIntersectionData.h"
#include "Data/PCGProjectionData.h"

const UPCGPointData* UPCGSpatialDataWithPointCache::ToPointData() const
{
	if (!CachedPointData)
	{
		CacheLock.Lock();

		if (!CachedPointData)
		{
			CachedPointData = CreatePointData();
		}

		CacheLock.Unlock();
	}

	return CachedPointData;
}

UPCGSpatialData* UPCGSpatialData::IntersectWith(const UPCGSpatialData* InOther) const
{
	UPCGIntersectionData* IntersectionData = NewObject<UPCGIntersectionData>(const_cast<UPCGSpatialData*>(this));
	IntersectionData->Initialize(this, InOther);

	return IntersectionData;
}

UPCGSpatialData* UPCGSpatialData::ProjectOn(const UPCGSpatialData* InOther) const
{
	UPCGProjectionData* ProjectionData = NewObject<UPCGProjectionData>(const_cast<UPCGSpatialData*>(this));
	ProjectionData->Initialize(this, InOther);

	return ProjectionData;
}