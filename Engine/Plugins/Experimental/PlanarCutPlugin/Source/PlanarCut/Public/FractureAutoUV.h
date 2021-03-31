// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "GeometryCollection/GeometryCollection.h"

#include "Image/ImageBuilder.h"

namespace UE { namespace PlanarCut {

/**
 * Make a UV atlas of non-overlapping UV charts for a geometry collection
 *
 * @param Collection		The collection to be atlas'd
 * @param UVRes				Target resolution for the atlas
 * @param GutterSize		Space to leave between UV islands, in pixels at the target resolution
 * @param bOnlyOddMaterials	If true, restrict UV island layout to odd-numbered material IDs
 * @param WhichMaterials	If non-empty, restrict UV island layout to only the listed material IDs
 * @param bRecreateUVsForDegenerateIslands If true, detect and fix islands that don't have proper UVs (i.e. UVs all zero or otherwise collapsed to a point)
 */
bool PLANARCUT_API UVLayout(
	FGeometryCollection& Collection,
	int32 UVRes = 1024,
	float GutterSize = 1,
	bool bOnlyOddMaterials = true,
	TArrayView<int32> WhichMaterials = TArrayView<int32>(),
	bool bRecreateUVsForDegenerateIslands = true
);


/**
 * Generate a texture for internal faces based on depth inside surface
 * TODO: add options to texture based on other quantities
 *
 * @param Collection		The collection to be create a new texture for
 * @param MaxDistance		Maximum distance to search for 'outside' surface, when computing depth inside surface
 * @param GutterSize		Number of texels to fill outside of UV island borders (values are copied from nearest inside pt)
 * @param TextureOut		Texture to write to
 * @param bOnlyOddMaterials	If true, restrict UV island layout to odd-numbered material IDs
 * @param WhichMaterials	If non-empty, restrict UV island layout to only the listed material IDs
 */
void PLANARCUT_API TextureInternalSurfaces(
	FGeometryCollection& Collection,
	double MaxDistance,
	int32 GutterSize,
	TImageBuilder<FVector3f>& TextureOut,
	bool bOnlyOddMaterials = true,
	TArrayView<int32> WhichMaterials = TArrayView<int32>()
);

}} // namespace UE::PlanarCut