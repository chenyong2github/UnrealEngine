// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreTypes.h"
#include "Templates/SharedPointer.h"

namespace UE::Geometry { class FDynamicMesh3; }
class UChaosClothAsset;
struct FManagedArrayCollection;

/**
 * Convert a single pattern from a ClothAsset to a FDynamicMesh3
 */
class CHAOSCLOTHASSETTOOLS_API FClothPatternToDynamicMesh
{
public:

	void Convert(const TSharedPtr<const FManagedArrayCollection> ClothCollection, int32 LODIndex, int32 PatternIndex, bool bGet2DPattern, UE::Geometry::FDynamicMesh3& MeshOut);

	void Convert(const UChaosClothAsset* ClothAssetMeshIn, int32 LODIndex, int32 PatternIndex, bool bGet2DPattern, UE::Geometry::FDynamicMesh3& MeshOut);
};
