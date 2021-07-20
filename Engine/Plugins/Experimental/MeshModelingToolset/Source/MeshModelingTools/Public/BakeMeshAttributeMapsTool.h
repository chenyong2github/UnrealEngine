// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"
#include "Sampling/MeshMapBaker.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "BakeMeshAttributeToolCommon.h"
#include "BakeMeshAttributeMapsTool.generated.h"


// predeclarations
struct FMeshDescription;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
PREDECLARE_GEOMETRY(template<typename RealType> class TMeshTangents);
PREDECLARE_GEOMETRY(class FMeshImageBakingCache);
using UE::Geometry::FImageDimensions;
class IPrimitiveComponentBackedTarget;
class IMeshDescriptionProvider;
class IMaterialProvider;
class FBakeNormalMapOp;
class FBakeOcclusionMapOp;
class FBakeCurvatureMapOp;
class FBakeMeshPropertyMapOp;
class FBakeTexture2DImageMapOp;

/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};



UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EBakeMapType
{
	None                   = 0 UMETA(Hidden),
	TangentSpaceNormalMap  = 1 << 0,
	AmbientOcclusion       = 1 << 1,
	BentNormal             = 1 << 2,
	Curvature              = 1 << 3,
	Texture2DImage         = 1 << 4,
	NormalImage            = 1 << 5,
	FaceNormalImage        = 1 << 6,
	PositionImage          = 1 << 7,
	MaterialID             = 1 << 8,
	MultiTexture           = 1 << 9,
	VertexColorImage       = 1 << 10,
	Occlusion              = (AmbientOcclusion | BentNormal) UMETA(Hidden),
	All                    = 0x7FF UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EBakeMapType);


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


UENUM()
enum class EBakeMultisampling
{
	None = 1 UMETA(DisplayName = "None"),
	Sample2x2 = 2 UMETA(DisplayName = "2 x 2"),
	Sample4x4 = 4 UMETA(DisplayName = "4 x 4"),
	Sample8x8 = 8 UMETA(DisplayName = "8 x 8"),
	Sample16x16 = 16 UMETA(DisplayName = "16 x 16")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The map types to generate */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(Bitmask, BitmaskEnum=EBakeMapType))
	int32 MapTypes = (int32) EBakeMapType::TangentSpaceNormalMap;

	/** The map type index to preview */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(ArrayClamp="Result"))
	int MapPreview = 0;

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution256;

	/** The multisampling configuration per texel */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	EBakeMultisampling Multisampling = EBakeMultisampling::None;

	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bUseWorldSpace = false;

	/** Distance to search for the correspondence between the source and target meshes */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = "0.001"))
	float Thickness = 3.0;

	/** Which UV layer to use to create the map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (GetOptions = GetUVLayerNamesFunc))
	FString UVLayer;

	UFUNCTION()
	TArray<FString> GetUVLayerNamesFunc();
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> UVLayerNamesList;

	UPROPERTY(VisibleAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	TArray<TObjectPtr<UTexture2D>> Result;

};


/**
 * Detail Map Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsTool : public UMultiSelectionTool, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshMapBaker>
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsTool() = default;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// IGenericDataOperatorFactory API
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshMapBaker>> MakeNewOperator() override;

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	UPROPERTY()
	TObjectPtr<UBakeMeshAttributeMapsToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<UBakedNormalMapToolProperties> NormalMapProps;

	UPROPERTY()
	TObjectPtr<UBakedOcclusionMapToolProperties> OcclusionMapProps;

	UPROPERTY()
	TObjectPtr<UBakedCurvatureMapToolProperties> CurvatureMapProps;

	UPROPERTY()
	TObjectPtr<UBakedTexture2DImageProperties> Texture2DProps;

	UPROPERTY()
	TObjectPtr<UBakedMultiTexture2DImageProperties> MultiTextureProps;

	UPROPERTY()
	TObjectPtr<UBakedOcclusionMapVisualizationProperties> VisualizationProps;


protected:
	friend class FMeshMapBakerOp;
	
	UDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

    UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BentNormalPreviewMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WorkingPreviewMaterial;
	float SecondsBeforeWorkingMaterial = 0.75;

	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UE::Geometry::FDynamicMesh3 BaseMesh;
	UE::Geometry::FDynamicMeshAABBTree3 BaseSpatial;

	bool bIsBakeToSelf = false;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	int32 DetailMeshTimestamp = 0;
	void UpdateDetailMesh();
	bool bDetailMeshValid = false;

	bool bInputsDirty = false;
	void UpdateResult();

	void UpdateOnModeChange();
	void UpdateVisualization();

	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshMapBaker>> Compute = nullptr;
	void OnMapsUpdated(const TUniquePtr<UE::Geometry::FMeshMapBaker>& NewResult);

	/** @return A single bitfield of map types from an array of map types. */
	EBakeMapType GetMapTypes(const int32& MapTypes) const;
	TArray<EBakeMapType> GetMapTypesArray(const int32& MapTypes) const;

	struct FBakeCacheSettings
	{
		EBakeMapType BakeMapTypes = EBakeMapType::None;
		FImageDimensions Dimensions;
		int32 UVLayer;
		int32 DetailTimestamp;
		float Thickness;
		int32 Multisampling;

		bool operator==(const FBakeCacheSettings& Other) const
		{
			return BakeMapTypes == Other.BakeMapTypes && Dimensions == Other.Dimensions &&
				UVLayer == Other.UVLayer && DetailTimestamp == Other.DetailTimestamp &&
				Thickness == Other.Thickness && Multisampling == Other.Multisampling;
		}
	};
	FBakeCacheSettings CachedBakeCacheSettings;
	TArray<EBakeMapType> ResultTypes;

	EBakeOpState OpState = EBakeOpState::Evaluate;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> CachedMaps;
	using CachedMapIndex = TMap<EBakeMapType, int32>;
	CachedMapIndex CachedMapIndices;

	FNormalMapSettings CachedNormalMapSettings;
	EBakeOpState UpdateResult_Normal();

	FOcclusionMapSettings CachedOcclusionMapSettings;
	EBakeOpState UpdateResult_Occlusion();

	FCurvatureMapSettings CachedCurvatureMapSettings;
	EBakeOpState UpdateResult_Curvature();

	FMeshPropertyMapSettings CachedMeshPropertyMapSettings;
	EBakeOpState UpdateResult_MeshProperty();

	TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> CachedTextureImage;
	FTexture2DImageSettings CachedTexture2DImageSettings;
	EBakeOpState UpdateResult_Texture2DImage();

	TMap<int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> CachedMultiTextures;
	EBakeOpState UpdateResult_MultiTexture();


	// empty maps are shown when nothing is computed
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyNormalMap;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapBlack;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapWhite;

	void InitializeEmptyMaps();

	void GetTexturesFromDetailMesh(const UPrimitiveComponent* DetailComponent);

};
