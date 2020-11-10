// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "GroomAssetCards.generated.h"


class UMaterialInterface;
class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EHairCardsClusterType : uint8
{
	Low,
	High,
};

UENUM(BlueprintType)
enum class EHairCardsGenerationType : uint8
{
	CardsCount,
	UseGuides,
};

/* Deprecated */
USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCardsClusterSettings
{
	GENERATED_BODY()

	FHairCardsClusterSettings();

	/** Decimation factor use to initialized cluster (only used when UseGuide is disabled). This changes the number of generated cards */
	UPROPERTY()
	float ClusterDecimation;

	/** Quality of clustering when group hair to cluster center. This does not change the number cards, but only how cards are shaped (size/shape) */
	UPROPERTY()
	EHairCardsClusterType Type;

	/** Use the simulation guide to generate the cards instead of the decimation parameter. This changes the number of generated cards. */
	UPROPERTY()
	bool bUseGuide;

	bool operator==(const FHairCardsClusterSettings& A) const;
};


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCardsGeometrySettings
{
	GENERATED_BODY()

	FHairCardsGeometrySettings();

	/** Define how cards should be generated. Cards count: define a targeted number of cards. Use guides: use simulation guide as cards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings")
	EHairCardsGenerationType GenerationType;

	/** Define how many cards should be generated. The generated number can be lower, as some cards can be discarded by other options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings", meta = (ClampMin = "1", UIMin = "1"))
	int32 CardsCount;

	/** Quality of clustering when group hair to belong to a similar cards. This does not change the number cards, but only how cards are shaped (size/shape) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ClusterSettings")
	EHairCardsClusterType ClusterType;

	/** Minimum size of a card segment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeometrySettings", AdvancedDisplay, meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "8.0", SliderExponent = 6))
	float MinSegmentLength;
	
	/** Max angular difference between adjacents vertices to remove vertices during simplification with MinSegmentLength, in degrees. */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ClampMin = "0", ClampMax = "45", UIMin = "0", UIMax = "45.0"))
	float AngularThreshold;

	/** Length below which generated cards are discard, as there are considered too small. (Default:0, which means no trimming) */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ClampMin = "0", UIMin = "0"))
	float MinCardsLength;

	/** Length above which generated cards are discard, as there are considered too larger. (Default:0, which means no trimming) */
	UPROPERTY(EditAnywhere, Category = "DecimationSettings", meta = (ClampMin = "0", UIMin = "0"))
	float MaxCardsLength;

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

	/* Deprecated */
	UPROPERTY()
	FHairCardsClusterSettings ClusterSettings;

	UPROPERTY(EditAnywhere, Category = "GeometrySettings", meta = (ToolTip = "Cards geometry settings"))
	FHairCardsGeometrySettings GeometrySettings;

	UPROPERTY(EditAnywhere, Category = "TextureSettings", meta = (ToolTip = "Cards texture atlast settings"))
	FHairCardsTextureSettings TextureSettings;

	/* Use to track when a cards asset need to be regenerated */
	UPROPERTY()
	int32 Version;

	bool operator==(const FHairGroupsProceduralCards& A) const;

	void BuildDDCKey(FArchive& Ar);
};

UENUM(BlueprintType)
enum class EHairCardsSourceType : uint8
{
	Procedural  UMETA(DisplayName = "Procedural (experimental)"),
	Imported UMETA(DisplayName = "Imported"),
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
struct HAIRSTRANDSCORE_API FHairGroupCardsTextures
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "CardsTextures")
	UTexture2D* DepthTexture = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsTextures")
	UTexture2D* CoverageTexture = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsTextures")
	UTexture2D* TangentTexture = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsAttributes")
	UTexture2D* AttributeTexture = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsAuxilaryData")
	UTexture2D* AuxilaryDataTexture = nullptr;

	bool bNeedToBeSaved = false;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsCardsSourceDescription
{
	GENERATED_BODY()

	FHairGroupsCardsSourceDescription();

	/* Deprecated */
	UPROPERTY()
	UMaterialInterface* Material = nullptr;

	UPROPERTY()
	FName MaterialSlotName;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	EHairCardsSourceType SourceType = EHairCardsSourceType::Imported;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	UStaticMesh* ProceduralMesh = nullptr;

	UPROPERTY()
	FString ProceduralMeshKey;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	UStaticMesh* ImportedMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	FHairGroupsProceduralCards ProceduralSettings;

	UPROPERTY(EditAnywhere, Category = "CardsSource")
	FHairGroupCardsTextures Textures;

	/* Group index on which this cards geometry will be used (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 GroupIndex = 0;
	
	/* LOD on which this cards geometry will be used. -1 means not used  (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 LODIndex = -1; 

	UPROPERTY(VisibleAnywhere, Transient, Category = "CardsSource")
	mutable FHairGroupCardsInfo CardsInfo;

	UPROPERTY(Transient)
	FString ImportedMeshKey;

	bool operator==(const FHairGroupsCardsSourceDescription& A) const;

	FString GetMeshKey() const;
	bool HasMeshChanged() const;
	void UpdateMeshKey();
};

