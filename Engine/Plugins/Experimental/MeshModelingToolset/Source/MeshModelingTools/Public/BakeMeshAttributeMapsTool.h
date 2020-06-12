// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Sampling/MeshSurfaceSampler.h"
#include "Image/ImageDimensions.h"
#include "BakeMeshAttributeMapsTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
template<typename RealType> class TMeshTangents;


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

	/**  */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bNormalMap = true;

	/**  */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bAmbientOcclusionMap = true;

	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution512;
};



UCLASS()
class MESHMODELINGTOOLS_API UBakedNormalMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = NormalMap, meta = (TransientToolProperty))
	UTexture2D* Result;

};


UCLASS()
class MESHMODELINGTOOLS_API UBakedOcclusionMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Number of AO rays */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "1", UIMax = "1024", ClampMin = "0", ClampMax = "50000"))
	int32 OcclusionRays = 256;

	/** Maximum AO distance (0 = infinity) */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0.0", UIMax = "1000.0", ClampMin = "0.0", ClampMax = "99999999.0"))
	float MaxDistance = 0;

	UPROPERTY(EditAnywhere, Category = OcclusionMap)
	bool bGaussianBlur = true;

	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	float BlurRadius = 2.25;

	UPROPERTY(VisibleAnywhere, Category = OcclusionMap, meta = (TransientToolProperty))
	UTexture2D* Result;

};



UCLASS()
class MESHMODELINGTOOLS_API UBakedOcclusionMapVisualizationProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float BaseGrayLevel = 0.7;

	/** AO Multiplier in visualization (does not affect output) */
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float OcclusionMultiplier = 1.0;
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
	virtual bool HasAccept() const override;
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
	UBakedOcclusionMapVisualizationProperties* VisualizationProps;



protected:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	USimpleDynamicMeshComponent* DynamicMeshComponent;

	TSharedPtr<FMeshDescription> BaseMeshDescription;
	TSharedPtr<TMeshTangents<double>> BaseMeshTangents;
	FDynamicMesh3 BaseMesh;
	FDynamicMeshAABBTree3 BaseSpatial;

	FDynamicMesh3 DetailMesh;
	FDynamicMeshAABBTree3 DetailSpatial;

	void InvalidateOcclusion();
	void InvalidateNormals();

	bool bResultValid;
	void UpdateResult();

	void UpdateVisualization();

	UPROPERTY()
	UMaterialInstanceDynamic* PreviewMaterial;

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

	struct FOcclusionMapSettings
	{
		FImageDimensions Dimensions;
		int32 OcclusionRays;
		float MaxDistance;
		float BlurRadius;

		bool operator==(const FOcclusionMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions && OcclusionRays == Other.OcclusionRays && MaxDistance == Other.MaxDistance && BlurRadius == Other.BlurRadius;
		}
	};
	FOcclusionMapSettings CachedOcclusionMapSettings;
	UPROPERTY()
	UTexture2D* CachedOcclusionMap;

	UPROPERTY()
	UTexture2D* EmptyNormalMap;

	UPROPERTY()
	UTexture2D* EmptyOcclusionMap;

	void InitializeEmptyMaps();

};
