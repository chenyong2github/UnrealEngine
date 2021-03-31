// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Image/ImageDimensions.h"
#include "BakeMeshAttributeMapsTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
template<typename RealType> class TMeshTangents;
class FMeshImageBakingCache;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UENUM()
enum class EBakeMapType
{
	TangentSpaceNormalMap,
	Occlusion,
	Curvature,
	Texture2DImage,
	NormalImage,
	FaceNormalImage,
	PositionImage
};


UENUM()
enum class EBakeTextureResolution 
{
	Resolution16 = 16 UMETA(DisplayName = "16 x 16"),
	Resolution32 = 32 UMETA(DisplayName = "32 x 32"),
	Resolution64 = 64 UMETA(DisplayName = "64 x 64"),
	Resolution128 = 128 UMETA(DisplayName = "128 x 128"),
	Resolution256 = 256 UMETA(DisplayName = "256 x 256"),
	Resolution512 = 512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 = 1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 = 2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 = 4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 = 8192 UMETA(DisplayName = "8192 x 8192")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The type of map to generate */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	EBakeMapType MapType = EBakeMapType::TangentSpaceNormalMap;

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution256;

	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bUseWorldSpace = false;

	UPROPERTY(EditAnywhere, Category = MapSettings)
	float Thickness = 3.0;

	/** Which UV layer to use to create the map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (GetOptions = GetUVLayerNamesFunc))
	FString UVLayer;

	UFUNCTION()
	TArray<FString> GetUVLayerNamesFunc();
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> UVLayerNamesList;

	UPROPERTY(VisibleAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	TArray<UTexture2D*> Result;

};


UENUM()
enum class ENormalMapSpace
{
	/** Tangent space */
	Tangent UMETA(DisplayName = "Tangent space"),
	/** Object space */
	Object UMETA(DisplayName = "Object space")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakedNormalMapToolProperties : public UInteractiveToolPropertySet
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

UENUM()
enum class EOcclusionMapPreview
{
	/** Ambient Occlusion */
	AmbientOcclusion UMETA(DisplayName = "Ambient Occlusion"),
	/** Bent Normal */
	BentNormal UMETA(DisplayName = "Bent Normal")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakedOcclusionMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Occlusion map output to preview */
	UPROPERTY(EditAnywhere, Category = OcclusionMap)
	EOcclusionMapPreview Preview = EOcclusionMapPreview::AmbientOcclusion;

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
class MESHMODELINGTOOLS_API UBakedOcclusionMapVisualizationProperties : public UInteractiveToolPropertySet
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
class MESHMODELINGTOOLS_API UBakedCurvatureMapToolProperties : public UInteractiveToolPropertySet
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
class MESHMODELINGTOOLS_API UBakedTexture2DImageProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The source texture that is to be resampled into a new texture map */
	UPROPERTY(EditAnywhere, Category = Texture2D, meta = (TransientToolProperty))
	UTexture2D* SourceTexture;

	/** The UV layer on the source mesh that corresponds to the SourceTexture */
	UPROPERTY(EditAnywhere, Category = Texture2D)
	int32 UVLayer = 0;
};




/**
 * Detail Map Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsTool();

	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	UPROPERTY()
	UBakeMeshAttributeMapsToolProperties* Settings;

	UPROPERTY()
	UBakedNormalMapToolProperties* NormalMapProps;

	UPROPERTY()
	UBakedOcclusionMapToolProperties* OcclusionMapProps;

	UPROPERTY()
	UBakedCurvatureMapToolProperties* CurvatureMapProps;

	UPROPERTY()
	UBakedTexture2DImageProperties* Texture2DProps;

	UPROPERTY()
	UBakedOcclusionMapVisualizationProperties* VisualizationProps;



protected:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UMaterialInstanceDynamic* PreviewMaterial;

	UPROPERTY()
	UMaterialInstanceDynamic* BentNormalPreviewMaterial;

	TSharedPtr<FMeshDescription> BaseMeshDescription;
	TSharedPtr<TMeshTangents<double>> BaseMeshTangents;
	FDynamicMesh3 BaseMesh;
	FDynamicMeshAABBTree3 BaseSpatial;

	bool bIsBakeToSelf = false;

	TSharedPtr<FDynamicMesh3> DetailMesh;
	TSharedPtr<FDynamicMeshAABBTree3> DetailSpatial;
	int32 DetailMeshTimestamp = 0;
	void UpdateDetailMesh();
	bool bDetailMeshValid = false;

	bool bResultValid;
	void UpdateResult();

	void UpdateOnModeChange();
	void UpdateVisualization();

	TPimplPtr<FMeshImageBakingCache> BakeCache;
	struct FBakeCacheSettings
	{
		FImageDimensions Dimensions;
		int32 UVLayer;
		int32 DetailTimestamp;
		float Thickness;

		bool operator==(const FBakeCacheSettings& Other) const
		{
			return Dimensions == Other.Dimensions && UVLayer == Other.UVLayer && DetailTimestamp == Other.DetailTimestamp && Thickness == Other.Thickness;
		}
	};
	FBakeCacheSettings CachedBakeCacheSettings;



	struct FNormalMapSettings
	{
		FImageDimensions Dimensions;

		bool operator==(const FNormalMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions;
		}
	};
	FNormalMapSettings CachedNormalMapSettings;
	UPROPERTY()
	UTexture2D* CachedNormalMap;

	void UpdateResult_Normal();



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
	FOcclusionMapSettings CachedOcclusionMapSettings;
	UPROPERTY()
	UTexture2D* CachedOcclusionMap;
	UPROPERTY()
	UTexture2D* CachedBentNormalMap;

	void UpdateResult_Occlusion();



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
	FCurvatureMapSettings CachedCurvatureMapSettings;
	UPROPERTY()
	UTexture2D* CachedCurvatureMap;

	void UpdateResult_Curvature();




	struct FMeshPropertyMapSettings
	{
		FImageDimensions Dimensions;
		int32 PropertyTypeIndex;

		bool operator==(const FMeshPropertyMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions && PropertyTypeIndex == Other.PropertyTypeIndex;
		}
	};
	FMeshPropertyMapSettings CachedMeshPropertyMapSettings;
	UPROPERTY()
	UTexture2D* CachedMeshPropertyMap;

	void UpdateResult_MeshProperty();




	struct FTexture2DImageSettings
	{
		FImageDimensions Dimensions;
		int32 UVLayer = 0;

		bool operator==(const FTexture2DImageSettings& Other) const
		{
			return Dimensions == Other.Dimensions && UVLayer == Other.UVLayer;
		}
	};
	FTexture2DImageSettings CachedTexture2DImageSettings;
	UPROPERTY()
	UTexture2D* CachedTexture2DImageMap;

	void UpdateResult_Texture2DImage();



	// empty maps are shown when nothing is computed

	UPROPERTY()
	UTexture2D* EmptyNormalMap;

	UPROPERTY()
	UTexture2D* EmptyColorMapBlack;

	UPROPERTY()
	UTexture2D* EmptyColorMapWhite;

	void InitializeEmptyMaps();

};
