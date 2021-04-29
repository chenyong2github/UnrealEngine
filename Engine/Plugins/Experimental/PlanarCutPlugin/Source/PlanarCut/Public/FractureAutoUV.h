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


// Different attributes we can bake
enum class EBakeAttributes : int32
{
	None,
	DistanceToExternal,
	AmbientOcclusion,
	Curvature,
	NormalZ,
	PositionZ
};

struct FTextureAttributeSettings
{
	double ToExternal_MaxDistance = 100.0;
	int AO_Rays = 32;
	double AO_BiasAngleDeg = 15.0;
	bool bAO_Blur = true;
	double AO_BlurRadius = 2.5;
	double AO_MaxDistance = 0.0; // 0.0 is interpreted as TNumericLimits<double>::Max()
	int Curvature_VoxelRes = 128;
	double Curvature_Winding = .5;
	int Curvature_SmoothingSteps = 10;
	double Curvature_SmoothingPerStep = .8;
	bool bCurvature_Blur = true;
	double Curvature_BlurRadius = 2.5;
	double Curvature_ThicknessFactor = 3.0; // distance to search for mesh correspondence, as a factor of voxel size
	double Curvature_MaxValue = .1; // curvatures above this value will be clamped
	
	bool bNormalZ_TakeAbs = true;
};


/**
 * Generate a texture for internal faces based on depth inside surface
 * TODO: add options to texture based on other quantities
 *
 * @param Collection		The collection to be create a new texture for
 * @param GutterSize		Number of texels to fill outside of UV island borders (values are copied from nearest inside pt)
 * @param BakeAttributes	Which attributes to bake into which color channel
 * @param AttributeSettings	Settings for the BakeAttributes
 * @param TextureOut		Texture to write to
 * @param bOnlyOddMaterials	If true, restrict UV island layout to odd-numbered material IDs
 * @param WhichMaterials	If non-empty, restrict UV island layout to only the listed material IDs
 */
void PLANARCUT_API TextureInternalSurfaces(
	FGeometryCollection& Collection,
	int32 GutterSize,
	UE::Geometry::FIndex4i BakeAttributes,
	const FTextureAttributeSettings& AttributeSettings,
	UE::Geometry::TImageBuilder<FVector4f>& TextureOut,
	bool bOnlyOddMaterials = true,
	TArrayView<int32> WhichMaterials = TArrayView<int32>()
);

}} // namespace UE::PlanarCut