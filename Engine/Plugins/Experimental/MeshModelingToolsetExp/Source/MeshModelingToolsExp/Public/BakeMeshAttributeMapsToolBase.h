// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolManager.h"
#include "AssetUtils/Texture2DBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Image/ImageDimensions.h"
#include "Sampling/MeshMapBaker.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "PreviewMesh.h"
#include "BakeMeshAttributeToolCommon.h"
#include "BakeMeshAttributeMapsToolBase.generated.h"

/**
* Bake maps enums
*/


UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EBakeMapType
{
	None                   = 0 UMETA(Hidden),
	/* Sample normals from the detail mesh in tangent space */
	TangentSpaceNormalMap  = 1 << 0 UMETA(DisplayName="Tangent Space Normals"),
	/* Sample ambient occlusion from the detail mesh */
	AmbientOcclusion       = 1 << 1 UMETA(DisplayName="Ambient Occlusion"),
	/* Sample normals skewed towards the least occluded direction from the detail mesh */
	BentNormal             = 1 << 2 UMETA(DisplayName="Bent Normals"),
	/* Sample mesh curvatures from the detail mesh */
	Curvature              = 1 << 3 UMETA(DisplayName="Curvature"),
	/* Sample a source texture from the detail mesh UVs */
	Texture2DImage         = 1 << 4 UMETA(DisplayName="Texture"),
	/* Sample object space normals from the detail mesh */
	NormalImage            = 1 << 5 UMETA(DisplayName="Object Space Normals"),
	/* Sample object space face normals from the detail mesh */
	FaceNormalImage        = 1 << 6 UMETA(DisplayName="Face Normals"),
	/* Sample bounding box relative positions from the detail mesh */
	PositionImage          = 1 << 7 UMETA(DisplayName="Position"),
	/* Sample material IDs as unique colors from the detail mesh */
	MaterialID             = 1 << 8 UMETA(DisplayName="Material ID"),
	/* Sample a source texture per material ID on the detail mesh */
	MultiTexture           = 1 << 9 UMETA(DisplayName="MultiTexture"),
	/* Sample the interpolated vertex colors from the detail mesh */
	VertexColorImage       = 1 << 10 UMETA(DisplayName="Vertex Colors"),
	Occlusion              = (AmbientOcclusion | BentNormal) UMETA(Hidden),
	All                    = 0x7FF UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EBakeMapType);


// Only include the Occlusion bitmask rather than its components
// (AmbientOcclusion | BentNormal). Since the Occlusion evaluator can
// evaluate both types in a single pass, only iterating over the Occlusion
// bitmask gives direct access to both types without the need to
// externally track if we've handled the Occlusion evaluator in a prior
// iteration loop.
static constexpr EBakeMapType ALL_BAKE_MAP_TYPES[] =
{
	EBakeMapType::TangentSpaceNormalMap,
	EBakeMapType::Occlusion, // (AmbientOcclusion | BentNormal)
	EBakeMapType::Curvature,
	EBakeMapType::Texture2DImage,
	EBakeMapType::NormalImage,
	EBakeMapType::FaceNormalImage,
	EBakeMapType::PositionImage,
	EBakeMapType::MaterialID,
	EBakeMapType::MultiTexture,
	EBakeMapType::VertexColorImage
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


UENUM()
enum class EBakeTextureFormat
{
	ChannelBits8 UMETA(DisplayName = "8 bits/channel"),
	ChannelBits16 UMETA(DisplayName = "16 bits/channel")
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


// TODO: Refactor common bake tool functionality with vertex bakes into common base class.
/**
 * Base Bake Maps tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsToolBase : public UMultiSelectionTool, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshMapBaker>
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsToolBase() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }
	// End UInteractiveTool interface

	// Begin IGenericDataOperatorFactory interface
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshMapBaker>> MakeNewOperator() override;
	// End IGenericDataOperatorFactory interface

	void SetWorld(UWorld* World);

protected:
	//
	// Tool property sets
	//
	UPROPERTY()
	TObjectPtr<UBakedOcclusionMapVisualizationProperties> VisualizationProps;

	//
	// Preview mesh and materials
	//
	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BentNormalPreviewMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WorkingPreviewMaterial;
	float SecondsBeforeWorkingMaterial = 0.75;

protected:
	/** To be invoked at end of client Setup methods. */
	void SetupBaseToolProperties();
	
	/**
	 * Process dirty props and update background compute.
	 * Invoked during Render.
	 */
	virtual void UpdateResult();

	/**
	 * Updates the preview material on the preview mesh with the
	 * computed results. Invoked by OnMapsUpdated.
	 */
	virtual void UpdateVisualization();


protected:
	//
	// Bake parameters
	//
	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld = nullptr;

	UE::Geometry::FDynamicMesh3 BaseMesh;
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UE::Geometry::FDynamicMeshAABBTree3 BaseSpatial;
	
	EBakeOpState OpState = EBakeOpState::Evaluate;

	struct FBakeCacheSettings
	{
		EBakeMapType BakeMapTypes = EBakeMapType::None;
		FImageDimensions Dimensions;
		EBakeTextureFormat SourceFormat = EBakeTextureFormat::ChannelBits8;
		int32 UVLayer = 0;
		int32 DetailTimestamp = 0;
		float Thickness = 3.0;
		int32 Multisampling = 1;

		bool operator==(const FBakeCacheSettings& Other) const
		{
			return BakeMapTypes == Other.BakeMapTypes && Dimensions == Other.Dimensions &&
				UVLayer == Other.UVLayer && DetailTimestamp == Other.DetailTimestamp &&
				Thickness == Other.Thickness && Multisampling == Other.Multisampling &&
				SourceFormat == Other.SourceFormat;
		}
	};
	FBakeCacheSettings CachedBakeCacheSettings;

	/**
	 * To be invoked by client when bake map types change.
	 */
	void OnMapTypesUpdated(int32 MapTypes);

	//
	// Background compute
	//
	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshMapBaker>> Compute = nullptr;

	/**
	 * Retrieves the result of the FMeshMapBaker and generates UTexture2D into the CachedMaps.
	 * It is the responsibility of the client to ensure that CachedMaps is appropriately sized for
	 * the range of index values in MapIndex.
	 * 
	 * @param NewResult the resulting FMeshMapBaker from the background Compute
	 */
	void OnMapsUpdated(const TUniquePtr<UE::Geometry::FMeshMapBaker>& NewResult, EBakeTextureFormat Format);

	/**
	 * Update the preview material parameters for a given a result index.
	 * @param PreviewIdx index into the ResultTypes array to preview
	 */
	void UpdatePreview(int PreviewIdx);

	/** Internal cache of bake texture results. */
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> CachedMaps;

	/** Internal map of map type to index into CachedMaps array */
	TMap<EBakeMapType, int32> CachedMapIndices;

	/** Internal array of CachedMaps index to map type */
	TArray<EBakeMapType> ResultTypes;
	
	//
	// Utilities
	//
	const bool bPreferPlatformData = false;
	
	/** @return An enumerated map type from a bitfield */
	static EBakeMapType GetMapTypes(const int32& MapTypes);

	/** @return An array of map types from a bitfield */
	static TArray<EBakeMapType> GetMapTypesArray(const int32& MapTypes);

	/** @return the Texture2D type for a given map type */
	static UE::Geometry::FTexture2DBuilder::ETextureType GetTextureType(EBakeMapType MapType, EBakeTextureFormat MapFormat);

	/** @return the texture name given a base name and map type */
	static void GetTextureName(EBakeMapType MapType, const FString& BaseName, FString& TexName);

	/**
	 * Given an array of textures associated with a material,
	 * use heuristics to identify the color/albedo texture.
	 * @param Textures array of textures associated with a material.
	 * @return integer index into the Textures array representing the color/albedo texture
	 */
	static int SelectColorTextureToBake(const TArray<UTexture*>& Textures);

	/**
	 * Iterate through a primitive component's textures by material ID.
	 * @param Component the component to query
	 * @param ProcessFunc enumeration function with signature: void(int MaterialID, const TArray<UTexture*>& Textures)
	 */
	template <typename ProcessFn>
	static void ProcessComponentTextures(const UPrimitiveComponent* Component, ProcessFn&& ProcessFunc);

	// empty maps are shown when nothing is computed
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyNormalMap;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapBlack;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapWhite;

	void InitializeEmptyMaps();
};


template <typename ProcessFn>
void UBakeMeshAttributeMapsToolBase::ProcessComponentTextures(const UPrimitiveComponent* Component, ProcessFn&& ProcessFunc)
{
	if (!Component)
	{
		return;
	}

	TArray<UMaterialInterface*> Materials;
	Component->GetUsedMaterials(Materials);
	
	for (int32 MaterialID = 0; MaterialID < Materials.Num(); ++MaterialID)	// TODO: This won't match MaterialIDs on the FDynamicMesh3 in general, will it?
	{
		UMaterialInterface* MaterialInterface = Materials[MaterialID];
		if (MaterialInterface == nullptr)
		{
			continue;
		}

		TArray<UTexture*> Textures;
		MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
		ProcessFunc(MaterialID, Textures);
	}
}



