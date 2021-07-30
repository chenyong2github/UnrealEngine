// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "MultiSelectionTool.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"
#include "BakeMeshAttributeToolCommon.generated.h"

// Pre-declarations
using UE::Geometry::FImageDimensions;
using UE::Geometry::TImageBuilder;


/**
 * Bake tool property sets
 */

UENUM()
enum class ENormalMapSpace
{
	/** Tangent space */
	Tangent UMETA(DisplayName = "Tangent space"),
	/** Object space */
	Object UMETA(DisplayName = "Object space")
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedNormalMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

};


UENUM()
enum class EOcclusionMapDistribution
{
	/** Uniform occlusion rays */
	Uniform UMETA(DisplayName = "Uniform"),
	/** Cosine weighted occlusion rays */
	Cosine UMETA(DisplayName = "Cosine")
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedOcclusionMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Number of occlusion rays */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "1", UIMax = "1024", ClampMin = "0", ClampMax = "50000"))
	int32 OcclusionRays = 16;

	/** Maximum occlusion distance (0 = infinity) */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0.0", UIMax = "1000.0", ClampMin = "0.0", ClampMax = "99999999.0"))
	float MaxDistance = 0;

	/** Maximum spread angle of occlusion rays. */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0", UIMax = "180.0", ClampMin = "0", ClampMax = "180.0"))
	float SpreadAngle = 180.0;

	/** Angular distribution of occlusion rays in the spread angle. */
	UPROPERTY(EditAnywhere, Category = OcclusionMap)
	EOcclusionMapDistribution Distribution = EOcclusionMapDistribution::Cosine;

	/** Whether or not to apply Gaussian Blur to computed AO Map (recommended) */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion")
	bool bGaussianBlur = true;

	/** Pixel Radius of Gaussian Blur Kernel */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion", meta = (UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	float BlurRadius = 2.25;

	/** Contribution of AO rays that are within this angle (degrees) from horizontal are attenuated. This reduces faceting artifacts. */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion", meta = (UIMin = "0", UIMax = "45.0", ClampMin = "0", ClampMax = "89.9"))
	float BiasAngle = 15.0;

	/** Coordinate space of the bent normal map. */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Bent Normal")
	ENormalMapSpace NormalSpace = ENormalMapSpace::Tangent;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedOcclusionMapVisualizationProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float BaseGrayLevel = 1.0;

	/** AO Multiplier in visualization (does not affect output) */
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float OcclusionMultiplier = 1.0;
};


UENUM()
enum class EBakedCurvatureTypeMode
{
	/** Mean Curvature is the average of the Max and Min Principal curvatures */
	MeanAverage,
	/** Max Principal Curvature */
	Max,
	/** Min Principal Curvature */
	Min,
	/** Gaussian Curvature is the product of the Max and Min Principal curvatures */
	Gaussian
};


UENUM()
enum class EBakedCurvatureColorMode
{
	/** Map curvature values to grayscale such that black is negative, grey is zero, and white is positive */
	Grayscale,
	/** Map curvature values to red/blue scale such that red is negative, black is zero, and blue is positive */
	RedBlue,
	/** Map curvature values to red/green/blue scale such that red is negative, green is zero, and blue is positive */
	RedGreenBlue
};


UENUM()
enum class EBakedCurvatureClampMode
{
	/** Include both negative and positive curvatures */
	None,
	/** Clamp negative curvatures to zero */
	Positive,
	/** Clamp positive curvatures to zero */
	Negative
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedCurvatureMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Type of curvature to compute */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureTypeMode CurvatureType = EBakedCurvatureTypeMode::MeanAverage;

	/** Color mapping calculated from curvature values */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureColorMode ColorMode = EBakedCurvatureColorMode::Grayscale;

	/** Scale the maximum curvature value used to compute the mapping to grayscale/color */
	UPROPERTY(EditAnywhere, Category = CurvatureMap, meta = (UIMin = "0.1", UIMax = "2.0", ClampMin = "0.001", ClampMax = "100.0"))
	float RangeMultiplier = 1.0;

	/** Scale the minimum curvature value used to compute the mapping to grayscale/color (fraction of maximum) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = CurvatureMap, meta = (UIMin = "0.0", UIMax = "1.0"))
	float MinRangeMultiplier = 0.0;

	/** Clamping to apply to curvature values before scaling to color range */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureClampMode Clamping = EBakedCurvatureClampMode::None;

	/** Whether or not to apply Gaussian Blur to computed Map */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	bool bGaussianBlur = false;

	/** Pixel Radius of Gaussian Blur Kernel */
	UPROPERTY(EditAnywhere, Category = CurvatureMap, meta = (UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	float BlurRadius = 2.25;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedTexture2DImageProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The source texture that is to be resampled into a new texture map */
	UPROPERTY(EditAnywhere, Category = Texture2D, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> SourceTexture;

	/** The UV layer on the source mesh that corresponds to the SourceTexture */
	UPROPERTY(EditAnywhere, Category = Texture2D)
	int32 UVLayer = 0;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakedMultiTexture2DImageProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** For each material ID, the source texture that will be resampled in that material's region*/
	UPROPERTY(EditAnywhere, Category = MultiTexture, meta = (DisplayName = "Material IDs / Source Textures"))
	TMap<int32, TObjectPtr<UTexture2D>> MaterialIDSourceTextureMap;

	/** UV layer to sample from on the input mesh */
	UPROPERTY(EditAnywhere, Category = MultiTexture)
	int32 UVLayer = 0;

	/** The set of all source textures from all input materials */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = MultiTexture, meta = (DisplayName = "Source Textures"))
	TArray<TObjectPtr<UTexture2D>> AllSourceTextures;

};


/**
 * Bake tool property settings structs
 */

struct FNormalMapSettings
{
	FImageDimensions Dimensions;

	bool operator==(const FNormalMapSettings& Other) const
	{
		return Dimensions == Other.Dimensions;
	}
};

struct FOcclusionMapSettings
{
	FImageDimensions Dimensions;
	int32 OcclusionRays;
	float MaxDistance;
	float SpreadAngle;
	EOcclusionMapDistribution Distribution;
	float BlurRadius;
	float BiasAngle;
	ENormalMapSpace NormalSpace;

	bool operator==(const FOcclusionMapSettings& Other) const
	{
		return Dimensions == Other.Dimensions &&
			OcclusionRays == Other.OcclusionRays &&
			MaxDistance == Other.MaxDistance &&
			SpreadAngle == Other.SpreadAngle &&
			Distribution == Other.Distribution &&
			BlurRadius == Other.BlurRadius &&
			BiasAngle == Other.BiasAngle &&
			NormalSpace == Other.NormalSpace;
	}
};

struct FCurvatureMapSettings
{
	FImageDimensions Dimensions;
	int32 RayCount = 1;
	int32 CurvatureType = 0;
	float RangeMultiplier = 1.0;
	float MinRangeMultiplier = 0.0;
	int32 ColorMode = 0;
	int32 ClampMode = 0;
	float MaxDistance = 1.0;
	float BlurRadius = 1.0;

	bool operator==(const FCurvatureMapSettings& Other) const
	{
		return Dimensions == Other.Dimensions && RayCount == Other.RayCount && CurvatureType == Other.CurvatureType && RangeMultiplier == Other.RangeMultiplier && MinRangeMultiplier == Other.MinRangeMultiplier && ColorMode == Other.ColorMode && ClampMode == Other.ClampMode && MaxDistance == Other.MaxDistance && BlurRadius == Other.BlurRadius;
	}
};

struct FMeshPropertyMapSettings
{
	FImageDimensions Dimensions;

	bool operator==(const FMeshPropertyMapSettings& Other) const
	{
		return Dimensions == Other.Dimensions;
	}
};

struct FTexture2DImageSettings
{
	FImageDimensions Dimensions;
	int32 UVLayer = 0;
	bool bSRGB = true;

	bool operator==(const FTexture2DImageSettings& Other) const
	{
		return Dimensions == Other.Dimensions && UVLayer == Other.UVLayer && bSRGB == Other.bSRGB;
	}
};


/**
 * Bake compute state
 */
enum class EBakeOpState
{
	Complete = 0,		// Inputs valid & Result is valid - no-op.
	Evaluate = 1 << 0,	// Inputs valid & Result is invalid - re-evaluate.
	Invalid	 = 1 << 1	// Inputs invalid - pause eval.
};
ENUM_CLASS_FLAGS(EBakeOpState);


