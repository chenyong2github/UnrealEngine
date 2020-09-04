// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "GroomAssetCards.generated.h"


class UMaterialInterface;
class UStaticMesh;


UENUM(BlueprintType)
enum class EHairCardsClusterType : uint8
{
	Low,
	High,
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCardsClusterSettings
{
	GENERATED_BODY()

	FHairCardsClusterSettings();

	/** Decimation factor to initialize cluster center (only used when UseGuide is disabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings", meta = (ClampMin = "0", UIMin = "0", UIMax = "1.0"))
	float ClusterDecimation;

	/** Quality of clustering when group hair to cluster center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings")
	EHairCardsClusterType Type;

	/** Use the simulation guide to generate the cards instead of the decimation parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings")
	bool bUseGuide;

	bool operator==(const FHairCardsClusterSettings& A) const;
};


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCardsGeometrySettings
{
	GENERATED_BODY()

	FHairCardsGeometrySettings();

	/** Number of cards per hair cluster */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeometrySettings", meta = (ClampMin = "1", UIMin = "1", UIMax = "5"))
	UPROPERTY()
	int32 CardsPerCluster;

	/** Minimum size of a card segment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeometrySettings", AdvancedDisplay, meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "8.0", SliderExponent = 6))
	float MinSegmentLength;
	
	/** Use the curve orientation to smoothly orient the cards */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeometrySettings", AdvancedDisplay)
	UPROPERTY()
	float UseCurveOrientation;

	bool operator==(const FHairCardsGeometrySettings& A) const;
};


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCardsTextureSettings
{
	GENERATED_BODY()

	FHairCardsTextureSettings();

	/** Max atlas resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AtlasSettings", meta = (ClampMin = "512", UIMin = "512", UIMax = "8192"))
	int32 AtlasMaxResolution;

	/** Pixel resolution per centimeters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AtlasSettings", meta = (ClampMin = "4", UIMin = "2", UIMax = "128"))
	int32 PixelPerCentimeters;

	/** Number of unique clump textures*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AtlasSettings", meta = (ClampMin = "1", UIMin = "1", UIMax = "128"))
	int32 LengthTextureCount;

	/** Number of texture having variation of strands count */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AtlasSettings", meta = (ClampMin = "1", UIMin = "1", UIMax = "128"))
	UPROPERTY()
	int32 DensityTextureCount;

	bool operator==(const FHairCardsTextureSettings& A) const;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsProceduralCards
{
	GENERATED_BODY()

	FHairGroupsProceduralCards();

	UPROPERTY(EditAnywhere, Category = "ClusterSettings", meta = (ToolTip = "Cards cluster settings"))
	FHairCardsClusterSettings ClusterSettings;

	UPROPERTY(EditAnywhere, Category = "GeometrySettings", meta = (ToolTip = "Cards geometry settings"))
	FHairCardsGeometrySettings GeometrySettings;

	UPROPERTY(EditAnywhere, Category = "TextureSettings", meta = (ToolTip = "Cards texture atlast settings"))
	FHairCardsTextureSettings TextureSettings;

	bool operator==(const FHairGroupsProceduralCards& A) const;
};

UENUM(BlueprintType)
enum class EHairCardsSourceType : uint8
{
	Procedural,
	Imported,
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupCardsInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Card Count"))
	int32 NumCards = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Card Vertex Count"))
	int32 NumCardVertices = 0;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsCardsSourceDescription
{
	GENERATED_BODY()

	FHairGroupsCardsSourceDescription();

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	UMaterialInterface* Material = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	EHairCardsSourceType SourceType = EHairCardsSourceType::Procedural;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	UStaticMesh* ImportedMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	FHairGroupsProceduralCards ProceduralSettings;

	/* Group index on which this cards geometry will be used (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 GroupIndex = 0;
	
	/* LOD on which this cards geometry will be used. -1 means not used  (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 LODIndex = -1; 

	UPROPERTY(VisibleAnywhere, Transient, Category = "CardsSource")
	mutable FHairGroupCardsInfo CardsInfo;

	bool operator==(const FHairGroupsCardsSourceDescription& A) const;
};

