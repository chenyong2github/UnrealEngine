// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"

struct FHairStrandsDatas;
struct FHairStrandsClusterCullingData;
struct FHairStrandsClusterCullingBulkData;
struct FHairGroupsLOD;
struct FHairGroupData;
struct FHairDescriptionGroups;
struct FHairGroupsInterpolation;
struct FHairStrandsInterpolationDatas;
struct FHairInterpolationSettings;
struct FHairStrandsBulkData;
struct FHairStrandsInterpolationBulkData;
struct FHairDescriptionGroup;
struct FHairGroupInfo;
class FHairDescription;
class UGroomAsset;

// Data flow overview
// ==================
// HairDescription -> HairDescriptionGroups -> HairStrandsData -> HairStrandsBulkData*
//															   -> HairStrandsInterpolationData ->HairStrandsInterpolationBulkData*
//															   -> HairStrandsClusterData*
//
// * Data used at runtime. Other type of data are intermediate data used only during building/within the editor
struct HAIRSTRANDSCORE_API FGroomBuilder
{
	static FString GetVersion();

	// 1. Build hair group based on the hair description
	static bool BuildHairDescriptionGroups(
		const FHairDescription& HairDescription, 
		FHairDescriptionGroups& Out);

	// 2.a Build FHairStrandsDatas for Strands & Guides, based on HairDescriptionGroups and DecimationSettings
	static void BuildData(
		const FHairDescriptionGroup& InHairDescriptionGroup,
		const FHairGroupsInterpolation& InSettings,
		FHairGroupInfo& OutGroupInfo,
		FHairStrandsDatas& OutStrands,
		FHairStrandsDatas& OutGuides);

	// 2.b Build FHairStrandsDatas for Strands or Guides. 
	// This version:
	// * Suppose OutStrands already contains curves & points data. 
	// * Compute only offset data/bounding box/max length/max radius/...
	static void BuildData(FHairStrandsDatas& OutStrands);

	// 3. Build bulk data for Strands / Guides
	static void BuildBulkData(
		const FHairGroupInfo& InInfo,
		const FHairStrandsDatas& InData,
		FHairStrandsBulkData& OutBulkData);

	// 4. Build interplation data based on the hairStrands data
	static void BuildInterplationData(
		const FHairGroupInfo& InInfo,
		const FHairStrandsDatas& InRenData,
		const FHairStrandsDatas& InSimData,
		const FHairInterpolationSettings& InInterpolationSettings,
		FHairStrandsInterpolationDatas& OutInterpolationData);

	// 5. Build interplation bulk data
	static void BuildInterplationBulkData(
		const FHairStrandsDatas& InSimData,
		const FHairStrandsInterpolationDatas& InInterpolationData,
		FHairStrandsInterpolationBulkData& OutInterpolationData);

	// 6. Build cluster data
	static void BuildClusterBulkData(
		const FHairStrandsDatas& InRenData,
		const float InGroomAssetRadius,
		const FHairGroupsLOD& InSettings,
		FHairStrandsClusterCullingBulkData& OutClusterCullingData);
};