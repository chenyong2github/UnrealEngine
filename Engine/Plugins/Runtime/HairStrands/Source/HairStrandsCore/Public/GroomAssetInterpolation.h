// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "PerPlatformProperties.h"
#include "GroomAssetInterpolation.generated.h"

UENUM()
enum class EHairInterpolationQuality : uint8
{
	Low		UMETA(DisplayName = "Low", ToolTip = "Build interpolation data based on nearst neighbor search. Low quality interpolation data, but fast to build (takes a few minutes)"),
	Medium	UMETA(DisplayName = "Medium", ToolTip = "Build interpolation data using curve shape matching search but within a limited spatial range. This is a tradeoff between Low and high quality in term of quality & build time (can takes several dozen of minutes)"),
	High	UMETA(DisplayName = "High", ToolTip = "Build interpolation data using curve shape matching search. This result in high quality interpolation data, but is relatively slow to build (can takes several dozen of minutes)"),
	Unknown	UMETA(Hidden),
};

UENUM()
enum class EHairInterpolationWeight : uint8
{
	Parametric	UMETA(DisplayName = "Parametric", ToolTip = "Build interpolation data based on curve parametric distance"),
	Root		UMETA(DisplayName = "Root", ToolTip = "Build interpolation data based on distance between guide's root and strands's root"),
	Index		UMETA(DisplayName = "Index", ToolTip = "Build interpolation data based on guide and strands vertex indices"),
	Unknown		UMETA(Hidden),
};

UENUM()
enum class EHairLODSelectionType : uint8
{
	Cpu		UMETA(DisplayName = "CPU", ToolTip = "Use CPU information for computing the LOD selection. This used the CPU object bounds, which is not always accurate, especially when the groom is bound to a skeletal mesh."),
	Gpu		UMETA(DisplayName = "GPU", ToolTip = "Use GPU information for computing the LOD selection. This use GPU hair clusters information which are more precise, but also more expensive to compute"),
};

UENUM(BlueprintType)
enum class EGroomGeometryType : uint8
{
	Strands,
	Cards,
	Meshes
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairLODSettings
{
	GENERATED_BODY();

	/** Reduce the number of hair strands */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float CurveDecimation = 1;

	/** Reduce the number of vertices for each hair strands */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of vertices per strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float VertexDecimation = 1;

	/** Max angular difference between adjacents vertices to remove vertices during simplification, in degrees. */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of vertices per strands in a uniform manner", ClampMin = "0", ClampMax = "45", UIMin = "0", UIMax = "45.0"))
	float AngularThreshold = 1.f;

	/** Screen size at which this LOD should be enabled */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float ScreenSize = 1;

	/** Scale the hair thickness for compensating the reduction of curves. This thickness scale properties manually increases/decreases the thickness in addition to 
	  * the automatic increase of thickness applied onto the curve when curve decimation is lower than 1. */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float ThicknessScale = 1;

	/** If true (default), the hair group is visible. If false, the hair group is culled. */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "If disable, the hair strands won't be rendered"))
	bool bVisible = true;

	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "If enable this LOD version will use cards representation"))
	EGroomGeometryType GeometryType = EGroomGeometryType::Strands;

	bool operator==(const FHairLODSettings& A) const;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairDecimationSettings
{
	GENERATED_BODY()

	FHairDecimationSettings();

	/** Reduce the number of hair strands in a uniform manner (initial decimation) */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float CurveDecimation;

	/**	Reduce the number of vertices for each hair strands in a uniform manner (initial decimation) */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of verties for each hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float VertexDecimation;

	bool operator==(const FHairDecimationSettings& A) const;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairInterpolationSettings
{
	GENERATED_USTRUCT_BODY()

	FHairInterpolationSettings();

	/** Flag to override the imported guides with generated guides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings", meta = (ToolTip = "If checked, override imported guides with generated ones."))
	bool bOverrideGuides;

	/** Density factor for converting hair into guide curve if no guides are provided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings", meta = (ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float HairToGuideDensity;

	/** Interpolation data quality. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings")
	EHairInterpolationQuality InterpolationQuality;

	/** Interpolation distance metric. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings")
	EHairInterpolationWeight InterpolationDistance;

	/** Randomize which guides affect a given hair strand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings")
	bool bRandomizeGuide;

	/** Force a hair strand to be affected by a unique guide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InterpolationSettings")
	bool bUseUniqueGuide;

	bool operator==(const FHairInterpolationSettings& A) const;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsInterpolation
{
	GENERATED_USTRUCT_BODY()

	FHairGroupsInterpolation();

	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Decimation settings"))
	FHairDecimationSettings DecimationSettings;

	UPROPERTY(EditAnywhere, Category = "InterpolationSettings", meta = (ToolTip = "Interpolation settings"))
	FHairInterpolationSettings InterpolationSettings;

	bool operator==(const FHairGroupsInterpolation& A) const;

	void BuildDDCKey(FArchive& Ar);
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsLOD
{
	GENERATED_USTRUCT_BODY()

	FHairGroupsLOD();

	/** LODs  */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "Reduce the number of hair strands in a uniform manner", ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	TArray<FHairLODSettings> LODs;

	/** Size of the hair clusters used for LOD decimation */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "If enable this LOD version will use cards representation"))
	float ClusterWorldSize;

	/** Size in pixel of a hair cluster when fully visible (screensize is equal to 1) */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ToolTip = "If enable this LOD version will use cards representation"))
	float ClusterScreenSizeScale;

	bool operator==(const FHairGroupsLOD& A) const;

	void BuildDDCKey(FArchive& Ar);
};
