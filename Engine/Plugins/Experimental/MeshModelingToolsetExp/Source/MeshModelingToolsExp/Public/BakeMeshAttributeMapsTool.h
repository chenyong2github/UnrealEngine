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
#include "PreviewMesh.h"
#include "BakeMeshAttributeMapsToolBase.h"
#include "BakeMeshAttributeMapsTool.generated.h"


// predeclarations
class UMaterialInstanceDynamic;
class UTexture2D;
PREDECLARE_GEOMETRY(template<typename RealType> class TMeshTangents);
PREDECLARE_GEOMETRY(class FMeshImageBakingCache);
using UE::Geometry::FImageDimensions;


/**
 * Tool Builder
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};






UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The map types to generate */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(Bitmask, BitmaskEnum=EBakeMapType))
	int32 MapTypes = (int32) EBakeMapType::None;

	/** The map type to preview */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(TransientToolProperty, GetOptions = GetMapPreviewNamesFunc))
	FString MapPreview;

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution256;

	/** The channel bit depth of the source data for the generated textures */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureFormat SourceFormat = EBakeTextureFormat::ChannelBits8;

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
	const TArray<FString>& GetUVLayerNamesFunc();
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> UVLayerNamesList;

	UFUNCTION()
	const TArray<FString>& GetMapPreviewNamesFunc();
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> MapPreviewNamesList;
	TMap<FString, FString> MapPreviewNamesMap;

	UPROPERTY(VisibleAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	TMap<EBakeMapType, TObjectPtr<UTexture2D>> Result;
};


/**
 * Detail Map Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsTool : public UBakeMeshAttributeMapsToolBase
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsTool() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;
	// End UInteractiveTool interface

	// Begin IGenericDataOperatorFactory interface
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshMapBaker>> MakeNewOperator() override;
	// End IGenericDataOperatorFactory interface

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	UPROPERTY()
	TObjectPtr<UBakeMeshAttributeMapsToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<UDetailMeshToolProperties> DetailMeshProps;

	UPROPERTY()
	TObjectPtr<UBakedOcclusionMapToolProperties> OcclusionMapProps;

	UPROPERTY()
	TObjectPtr<UBakedCurvatureMapToolProperties> CurvatureMapProps;

	UPROPERTY()
	TObjectPtr<UBakedTexture2DImageProperties> Texture2DProps;

	UPROPERTY()
	TObjectPtr<UBakedMultiTexture2DImageProperties> MultiTextureProps;

protected:
	// Begin UBakeMeshAttributeMapsToolBase interface
	virtual void UpdateResult() override;
	virtual void UpdateVisualization() override;

	virtual void GatherAnalytics(FBakeAnalytics::FMeshSettings& Data) override;
	// End UBakeMeshAttributeMapsToolBase interface

protected:
	friend class FMeshMapBakerOp;

	bool bIsBakeToSelf = false;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> DetailMeshTangents;
	int32 DetailMeshTimestamp = 0;
	void UpdateDetailMesh();
	bool bDetailMeshValid = false;

	void UpdateOnModeChange();

	void InvalidateResults();

	FDetailMeshSettings CachedDetailMeshSettings;
	TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> CachedDetailNormalMap;
	EBakeOpState UpdateResult_DetailNormalMap();

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
};

