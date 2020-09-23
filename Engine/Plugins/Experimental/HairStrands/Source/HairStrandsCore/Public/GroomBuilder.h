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

	static bool BuildGroom(FProcessedHairDescription& ProcessedHairDescription, UGroomAsset* GroomAsset, uint32 GroupIndex);
	static bool BuildGroom(const FHairDescription& HairDescription, UGroomAsset* GroomAsset);

	static void BuildClusterData(const FHairStrandsDatas& RenStrandsData, const float InGroomAssetRadius, const FHairGroupsLOD& InSettings, FHairStrandsClusterCullingData& Out);
};