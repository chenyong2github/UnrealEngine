// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneTypes.h"

class AColorCorrectRegion;

class FColorCorrectRegionDatabase
{
public:
	/** 
	* Stores First Component Ids statically in a map with CCR pointer as a key. Thread safe.
	*/
	static void UpdateCCRDatabaseFirstComponentId(const AColorCorrectRegion* InCCR, FPrimitiveComponentId ComponentId)
	{
		if (!FirstPrimitiveIds.Contains(InCCR))
		{
			FScopeLock RegionScopeLock(&FirstPrimitiveIdCriticalSection);
			FirstPrimitiveIds.Add(InCCR, ComponentId);
		}
		else if (!(FirstPrimitiveIds[InCCR] == ComponentId))
		{
			FScopeLock RegionScopeLock(&FirstPrimitiveIdCriticalSection);
			FirstPrimitiveIds[InCCR] = ComponentId;
		}
	}

	/** 
	* Used on render thread to check Primitive Component ID if it is hidden in view. Thread safe.
	*/
	static FPrimitiveComponentId GetFirstComponentId(const AColorCorrectRegion* InCCR)
	{
		FScopeLock RegionScopeLock(&FirstPrimitiveIdCriticalSection);
		if (FirstPrimitiveIds.Contains(InCCR))
		{
			return FirstPrimitiveIds[InCCR];
		}
		return FPrimitiveComponentId();
	}

	/** 
	* Cleanup associated data when CCR is removed from the scene.
	*/
	static void RemoveCCRData(const AColorCorrectRegion* InCCR)
	{
		if (FirstPrimitiveIds.Contains(InCCR))
		{
			FScopeLock RegionScopeLock(&FirstPrimitiveIdCriticalSection);
			FirstPrimitiveIds.Remove(InCCR);
		}
	}
private:

	static FCriticalSection FirstPrimitiveIdCriticalSection;

	/** 
	* Stores the first primitive's IDs of each region to be used on render tread by Scene View Extension 
	* to check if CCR needs to be hidden
	*/
	static TMap<const AColorCorrectRegion*, FPrimitiveComponentId> FirstPrimitiveIds;
};