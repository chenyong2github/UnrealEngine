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
	int32 HairCount;

	/** Number of simulation guides within this hair group. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	int32 GuideCount;

	/** Length of the longest hair strands */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Groom")
	float HairLength;

	/** Hair width (in centimeters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "1.0", SliderExponent = 6))
	float HairWidth;

	/** Scale the hair width at the root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "2.0", SliderExponent = 6))
	float HairRootScale;

	/** Scale the hair with at the tip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "2.0", SliderExponent = 6))
	float HairTipScale;

	/** Normalized hair clip length, i.e. at which length hair will be clipped. 1 means no clipping. 0 means hairs are fully clipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0", SliderExponent = 1))
	float HairClipLength;

	/** Override the hair shadow density factor (unit less). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairShadowDensity;

	/** Scale the hair geometry radius for ray tracing effects (e.g. shadow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Groom", AdvancedDisplay, meta = (ClampMin = "0.0001", UIMin = "0.001", UIMax = "10.0", SliderExponent = 6))
	float HairRaytracingRadiusScale;

	/** The targeted budget of hair vertex per pixel. Cluster strands will be decimated based on that. Used when r.HairStrands.Cluster.Culling = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "6.0", SliderExponent = 1))
	float LodAverageVertexPerPixel;
	/** Bias the selected LOD. A value >0 will progressively select lower detailed lods. Used when r.HairStrands.Cluster.Culling = 1. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Performance", AdvancedDisplay, meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0", SliderExponent = 1))
	float LodBias;

	// Contructor used to initialised new values
	FHairGroupDesc()
	{
		ReInit();
	}

	void ReInit()
	{
		HairRaytracingRadiusScale = 1.0f;
		HairRootScale = 1.0f;
		HairTipScale = 1.0f;
		HairClipLength = 1.0f;

		LodAverageVertexPerPixel = 3.0f; // Good for quality on heavy assets
		LodBias = 0.0f;
	}
};
