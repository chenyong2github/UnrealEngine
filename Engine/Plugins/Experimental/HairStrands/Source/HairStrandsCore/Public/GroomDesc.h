// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "GroomDesc.generated.h"

USTRUCT(BlueprintType)
struct FHairGroupDesc
{
	GENERATED_USTRUCT_BODY()

	/** Number of hairs within this hair group. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	int32 HairCount = 0;

	/** Number of simulation guides within this hair group. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	int32 GuideCount = 0;

	/** Length of the longest hair strands */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	float HairLength = 0;

	/** Hair width (in centimeters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "1.0", SliderExponent = 6))
	float HairWidth = 0;

	/** Scale the hair width at the root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "2.0", SliderExponent = 6))
	float HairRootScale = 0;

	/** Scale the hair with at the tip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "2.0", SliderExponent = 6))
	float HairTipScale = 0;

	/** Normalized hair clip length, i.e. at which length hair will be clipped. 1 means no clipping. 0 means hairs are fully clipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0", SliderExponent = 1))
	float HairClipLength = 0;

	/** Override the hair shadow density factor (unit less). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairShadowDensity = 0;

	/** Scale the hair geometry radius for ray tracing effects (e.g. shadow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairRaytracingRadiusScale = 0;

	/** Bias the selected LOD. A value >0 will progressively select lower detailed lods. Used when r.HairStrands.Cluster.Culling = 1. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Performance", AdvancedDisplay, meta = (ClampMin = "-7.0", ClampMax = "7.0", UIMin = "-7.0", UIMax = "7.0", SliderExponent = 1))
	float LODBias = 0;

	/** Insure the hair does not alias. When enable, group of hairs might appear thicker. Isolated hair should remain thin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	bool bUseStableRasterization = false;

	/** Light hair with the scene color. This is used for vellus/short hair to bring light from the surrounding surface, like skin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Groom")
	bool bScatterSceneLighting = false;

	UPROPERTY()
	bool bSupportVoxelization = true;

	/** Force a specific LOD index */
	UPROPERTY()
	int32 LODForcedIndex = -1;
};
typedef FHairGroupDesc FHairGroupInstanceModifer;