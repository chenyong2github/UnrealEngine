// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"

struct FHairStrandsDatas;
struct FHairStrandsClusterCullingData;
struct FHairGroupsLOD;
struct FHairGroupData;
struct FProcessedHairDescription;
struct FHairGroupsInterpolation;
struct FHairStrandsInterpolationDatas;
struct FHairInterpolationSettings;
class FHairDescription;
class UGroomAsset;

struct HAIRSTRANDSCORE_API FGroomBuilder
{
	static FString GetVersion();

	static bool ProcessHairDescription(const FHairDescription& HairDescription, FProcessedHairDescription& Out);
	static void Decimate(const FHairStrandsDatas& InData, float CurveDecimationPercentage, float VertexDecimationPercentage, FHairStrandsDatas& OutData);

	static void BuildData(FHairGroupData& GroupData, const FHairGroupsInterpolation& InterpolationSettings, uint32 GroupIndex);
	static void BuildData(UGroomAsset* GroomAsset);
	static void BuildData(FHairStrandsDatas& RenData, FHairStrandsDatas& SimData, FHairStrandsInterpolationDatas& InterpolationData, const FHairInterpolationSettings& InterpolationSettings, const bool bBuildRen, const bool bBuildSim, const bool bBuildInterpolation, uint32 Seed);

	/** Build data for fully functional GroomAsset including HairGroupData, render and interpolation data */
	static bool BuildGroom(FProcessedHairDescription& ProcessedHairDescription, UGroomAsset* GroomAsset, uint32 GroupIndex);
	static bool BuildGroom(const FHairDescription& HairDescription, UGroomAsset* GroomAsset);

	/** Build only the HairGroupData */
	static void BuildHairGroupData(FProcessedHairDescription& ProcessedHairDescription, const FHairGroupsInterpolation& InSettings, uint32 GroupIndex, FHairGroupData& OutHairGroupData);

	static float ComputeGroomBoundRadius(const FProcessedHairDescription& Description);
	static float ComputeGroomBoundRadius(const TArray<FHairGroupData>& HairGroupsData);

	static void BuildClusterData(UGroomAsset* GroomAsset, const float InGroomAssetRadius);
	static void BuildClusterData(UGroomAsset* GroomAsset, const float InGroomAssetRadius, uint32 GroupIndex);
	static void BuildClusterData(UGroomAsset* GroomAsset, const FProcessedHairDescription& ProcessedHairDescription);
	static void BuildClusterData(UGroomAsset* GroomAsset, const FProcessedHairDescription& ProcessedHairDescription, uint32 GroupIndex);
};