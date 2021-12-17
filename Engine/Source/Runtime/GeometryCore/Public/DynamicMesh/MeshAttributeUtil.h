// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"


/**
 * Utility functions for manipulating DynamicMesh attribute sets
 */
namespace UE
{
namespace Geometry
{

	/**
	 * Compact the values of an integer Triangle Attribute, ie so that the attribute values are dense in range 0..N.
	 * Useful for (eg) compacting MaterialIDs or Polygroups.
	 * @param OldToNewMapOut generated mapping from previous IDs to new IDs (ie size is maximum input attrib value (+1), will contain InvalidID for any compacted values )
	 * @param NewToOldMapOut generated mapping from new IDs to previous IDs (ie size is maximum output attribute value (+1), no invalid values)
	 * @param bWasCompactOut set to true if the attribute set was already compact, IE was not modified by the operation
	 * @return true on success (including if already compact). false if any Attribute Values were negative/InvalidID, cannot compact in this case
	 */
	GEOMETRYCORE_API bool CompactAttributeValues(
		const FDynamicMesh3& Mesh,
		TDynamicMeshScalarTriangleAttribute<int32>& TriangleAttrib,
		TArray<int32>& OldToNewMapOut,
		TArray<int32>& NewToOldMapOut,
		bool& bWasCompactOut);


}
}
