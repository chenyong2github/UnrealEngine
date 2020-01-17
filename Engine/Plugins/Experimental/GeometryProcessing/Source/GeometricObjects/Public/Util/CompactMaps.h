// Copyright Epic Games, Inc. All Rights Reserved.

// Index remapping struct extracted from Dynamic Mesh for more general use

#pragma once

#include "Containers/Array.h"

#include "IndexTypes.h"

/**
 * Stores index remapping arrays for different types of elements
 * Should only be used for compacting, and should maintain invariant that Map*[Idx] <= Idx for all maps
 */
struct FCompactMaps
{
	constexpr static int InvalidID = IndexConstants::InvalidID;

	TArray<int> MapV;
	TArray<int> MapT;
	bool bKeepTriangleMap = true; // if set to false, MapT should be empty

	/**
	 * Set up maps as identity maps.
	 * Note triangle map will not be set if bKeepTriangleMap is false.
	 *
	 * @param MaxVID Vertex map will be created from 0 to MaxVID
	 * @param MaxTID If bKeepTriangleMap is true, triangle map will be created from 0 to MaxTID
	 */
	void SetIdentity(int MaxVID, int MaxTID)
	{
		MapV.SetNumUninitialized(MaxVID);
		for (int ID = 0; ID < MaxVID; ID++)
		{
			MapV[ID] = ID;
		}
		if (bKeepTriangleMap)
		{
			MapT.SetNumUninitialized(MaxTID);
			for (int ID = 0; ID < MaxTID; ID++)
			{
				MapT[ID] = ID;
			}
		}
		else
		{
			MapT.Empty();
		}
	}

	/**
	 * Reset all maps, and initialize with InvalidID
	 * @param MaxVID Size of post-reset vertex map (filled w/ InvalidID)
	 * @param MaxTID Size of post-reset triangle map (filled w/ InvalidID)
	 */
	void Reset(int MaxVID, int MaxTID = 0)
	{
		MapV.Reset();
		MapT.Reset();
		MapV.SetNumUninitialized(MaxVID);
		MapT.SetNumUninitialized(MaxTID);
		for (int VID = 0; VID < MaxVID; VID++)
		{
			MapV[VID] = InvalidID;
		}
		for (int TID = 0; TID < MaxTID; TID++)
		{
			MapT[TID] = InvalidID;
		}
	}

	/** Reset all maps, leaving them empty */
	void Reset()
	{
		MapV.Reset();
		MapT.Reset();
	}

	/** Set mapping for a vertex */
	inline void SetVertex(int FromID, int ToID)
	{
		checkSlow(FromID >= ToID);
		MapV[FromID] = ToID;
	}

	/** Set mapping for a triangle */
	inline void SetTriangle(int FromID, int ToID)
	{
		checkSlow(bKeepTriangleMap);
		checkSlow(FromID >= ToID);
		MapT[FromID] = ToID;
	}

	/** Get mapping for a vertex */
	inline int GetVertex(int FromID) const
	{
		int ToID = MapV[FromID];
		checkSlow(ToID != InvalidID);
		return ToID;
	}

	/** Get mapping for a triangle */
	inline int GetTriangle(int FromID) const
	{
		checkSlow(bKeepTriangleMap);
		int ToID = MapT[FromID];
		checkSlow(ToID != InvalidID);
		return ToID;
	}

	/** 
	 * Clear all triangles
	 *
	 * @param bPermanent If true, set flag to not create triangle map
	 */
	void ClearTriangleMap(bool bPermanent = true)
	{
		if (bPermanent)
		{
			bKeepTriangleMap = false;
		}
		MapT.Empty();
	}

	/** Check data for validity; for testing */
	bool Validate() const
	{
		for (int Idx = 0; Idx < MapV.Num(); Idx++)
		{
			if (MapV[Idx] > Idx)
			{
				return false;
			}
		}
		for (int Idx = 0; Idx < MapT.Num(); Idx++)
		{
			if (MapT[Idx] > Idx)
			{
				return false;
			}
		}
		if (!bKeepTriangleMap && MapT.Num() > 0)
		{
			return false;
		}
		return true;
	}
};
