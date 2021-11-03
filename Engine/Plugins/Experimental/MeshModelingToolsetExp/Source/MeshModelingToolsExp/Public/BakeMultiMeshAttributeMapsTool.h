// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"
#include "Sampling/MeshMapBaker.h"
#include "Scene/MeshSceneAdapter.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "PreviewMesh.h"
#include "BakeMeshAttributeMapsToolBase.h"
#include "BakeMultiMeshAttributeMapsTool.generated.h"

/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMultiMeshAttributeMapsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

	public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMultiMeshAttributeMapsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The map types to generate */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(Bitmask, BitmaskEnum=EBakeMapType, ValidEnumValues="TangentSpaceNormal, Texture"))
	int32 MapTypes = (int32) EBakeMapType::None;

	/** The map type index to preview */
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

	/** Distance to search for the correspondence between the source and target meshes */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = "0.001"))
	float Thickness = 3.0;

	/** The base mesh UV layer to use to create the map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (GetOptions = GetUVLayerNamesFunc))
	FString UVLayer;

	UFUNCTION()
	TArray<FString> GetUVLayerNamesFunc();
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


USTRUCT()
struct MESHMODELINGTOOLSEXP_API FBakeMultiMeshDetailProperties
{
	GENERATED_BODY()

	/** The detail mesh to sample */
	UPROPERTY(VisibleAnywhere, Category = DetailMesh, meta = (TransientToolProperty))
	TObjectPtr<UStaticMesh> DetailMesh = nullptr;

	/** The detail mesh normal map to sample. If empty, the geometric normals will be used. */
	UPROPERTY(EditAnywhere, Category = DetailMesh, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> DetailColorMap = nullptr;

	/** UV layer to sample from on the detail mesh */
	UPROPERTY(EditAnywhere, Category = DetailMesh, meta = (TransientToolProperty))
	int32 DetailColorMapUVLayer = 0;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMultiMeshDetailToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty, EditFixedSize))
	TArray<FBakeMultiMeshDetailProperties> DetailProperties;
};


struct FBakeMultiMeshDetailSettings
{
	using FColorMapData = TTuple<int32, bool>;
	TArray<FColorMapData> ColorMapData;
	
	bool operator==(const FBakeMultiMeshDetailSettings& Other) const
	{
		const int NumData = ColorMapData.Num();
		bool bIsEqual = Other.ColorMapData.Num() == NumData;
		for (int Idx = 0; bIsEqual && Idx < NumData; ++Idx)
		{
			bIsEqual = bIsEqual && ColorMapData[Idx] == Other.ColorMapData[Idx];
		}
		return bIsEqual;
	}
};


// TODO: Refactor shared code into common base class.
/**
 * N-to-1 Detail Map Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMultiMeshAttributeMapsTool : public UBakeMeshAttributeMapsToolBase
{
	GENERATED_BODY()

public:
	UBakeMultiMeshAttributeMapsTool() = default;

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
	TObjectPtr<UBakeMultiMeshAttributeMapsToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<UBakeMultiMeshDetailToolProperties> DetailProps;


protected:
	// Begin UBakeMeshAttributeMapsToolBase interface
	virtual void UpdateResult() override;
	virtual void UpdateVisualization() override;

	virtual void GatherAnalytics(FBakeAnalytics::FMeshSettings& Data) override;
	// End UBakeMeshAttributeMapsToolBase interface
	

protected:
	friend class FMultiMeshMapBakerOp;

	UE::Geometry::FMeshSceneAdapter DetailMeshScene;

	void UpdateOnModeChange();

	void InvalidateResults();

	// Cached detail mesh data
	FBakeMultiMeshDetailSettings CachedDetailSettings;
	EBakeOpState UpdateResult_DetailMeshes();
	
	using FTextureImageData = TTuple<UE::Geometry::TImageBuilder<FVector4f>*, int>;
	using FTextureImageMap = TMap<void*, UE::Geometry::IMeshBakerDetailSampler::FBakeDetailTexture>; 
	TArray<TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>>> CachedColorImages;
	TArray<int> CachedColorUVLayers;
	FTextureImageMap CachedMeshToColorImagesMap;

	// Analytics
	virtual FString GetAnalyticsEventName() const override
	{
		return TEXT("BakeAll");
	}
};
