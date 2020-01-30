// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"

struct FHairStrandsDatas;
class UGroomAsset;

struct HAIRSTRANDSCORE_API FGroomBuilder
{
	static bool BuildGroom(const class FHairDescription& HairDescription, const struct FGroomBuildSettings& BuildSettings, UGroomAsset* GroomAsset);
	static void GenerateGuides(const FHairStrandsDatas& InData, float DecimationPercentage, FHairStrandsDatas& OutData);
	static void BuildData(UGroomAsset* GroomAsset, const FGroomBuildSettings& BuildSettings);
};