// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolQueryInterfaces.h" // for UInteractiveToolExclusiveToolAPI
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
	TangentSpaceNormal     = 1 << 0,
	/* Sample ambient occlusion from the detail mesh */
	AmbientOcclusion       = 1 << 1,
	/* Sample normals skewed towards the least occluded direction from the detail mesh */
	BentNormal             = 1 << 2,
	/* Sample mesh curvatures from the detail mesh */
	Curvature              = 1 << 3,
	/* Sample a source texture from the detail mesh UVs */
	Texture                = 1 << 4,
	/* Sample object space normals from the detail mesh */
	ObjectSpaceNormal      = 1 << 5,
	/* Sample object space face normals from the detail mesh */
	FaceNormal             = 1 << 6,
	/* Sample bounding box relative positions from the detail mesh */
	Position               = 1 << 7,
	/* Sample material IDs as unique colors from the detail mesh */
	MaterialID             = 1 << 8 UMETA(DisplayName="Material ID"),
	/* Sample a source texture per material ID on the detail mesh */
	MultiTexture           = 1 << 9,
	/* Sample the interpolated vertex colors from the detail mesh */
	VertexColor            = 1 << 10,
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
	EBakeMapType::TangentSpaceNormal,
	EBakeMapType::Occlusion, // (AmbientOcclusion | BentNormal)
	EBakeMapType::Curvature,
	EBakeMapType::Texture,
	EBakeMapType::ObjectSpaceNormal,
	EBakeMapType::FaceNormal,
	EBakeMapType::Position,
	EBakeMapType::MaterialID,
	EBakeMapType::MultiTexture,
	EBakeMapType::VertexColor
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
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsToolBase : public UMultiSelectionTool, public IInteractiveToolExclusiveToolAPI, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshMapBaker>
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

public:
	/** @return An enumerated map type from a bitfield */
	static EBakeMapType GetMapTypes(const int32& MapTypes);

	/** Internal maps of name to BakeMapType */
	static TMap<FString, EBakeMapType> NameToMapTypeMap;
	static TMap<EBakeMapType, FString> MapTypeToNameMap;

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
	/**
	 * Post-client setup function. Should be invoked at end of client Setup().
	 * Initialize common base tool properties (ex. visualization properties) and
	 * analytics.
	 */
	void PostSetup();
	
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

	/**
	 * Invalidates the background compute operator.
	 */
	void InvalidateCompute();

	/**
	 * Create texture assets from our result map of Texture2D
	 * @param Textures the result map of textures to create
	 * @param SourceWorld the source world to define where the texture assets will be stored.
	 * @param SourceAsset if not null, result textures will be stored adjacent to this asset.
	 */
	void CreateTextureAssets(const TMap<EBakeMapType, TObjectPtr<UTexture2D>>& Textures, UWorld* SourceWorld, UObject* SourceAsset);


protected:
	//
	// Bake parameters
	//
	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld = nullptr;

	UE::Geometry::FDynamicMesh3 BaseMesh;
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UE::Geometry::FDynamicMeshAABBTree3 BaseSpatial;

	bool bInputsDirty = false;
	EBakeOpState OpState = EBakeOpState::Evaluate;

	struct FBakeCacheSettings
	{
		EBakeMapType SourceBakeMapTypes = EBakeMapType::None;
		EBakeMapType BakeMapTypes = EBakeMapType::None;
		FImageDimensions Dimensions;
		EBakeTextureFormat SourceFormat = EBakeTextureFormat::ChannelBits8;
		int32 UVLayer = 0;
		int32 DetailTimestamp = 0;
		float Thickness = 3.0;
		int32 Multisampling = 1;
		bool bUseWorldSpace = false;

		bool operator==(const FBakeCacheSettings& Other) const
		{
			return BakeMapTypes == Other.BakeMapTypes && Dimensions == Other.Dimensions &&
				UVLayer == Other.UVLayer && DetailTimestamp == Other.DetailTimestamp &&
				Thickness == Other.Thickness && Multisampling == Other.Multisampling &&
				SourceFormat == Other.SourceFormat && SourceBakeMapTypes == Other.SourceBakeMapTypes &&
				bUseWorldSpace == Other.bUseWorldSpace;
		}
	};
	FBakeCacheSettings CachedBakeCacheSettings;

	/**
	 * To be invoked by client when bake map types change.
	 */
	template <typename PropertySet>
	void OnMapTypesUpdated(PropertySet& Properties);

	//
	// Background compute
	//
	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshMapBaker>> Compute = nullptr;

	/** Internal cache of bake texture results. */
	UPROPERTY()
	TMap<EBakeMapType, TObjectPtr<UTexture2D>> CachedMaps;

	/**
	 * Retrieves the result of the FMeshMapBaker and generates UTexture2D into the CachedMaps.
	 * It is the responsibility of the client to ensure that CachedMaps is appropriately sized for
	 * the range of index values in MapIndex.
	 * 
	 * @param NewResult the resulting FMeshMapBaker from the background Compute
	 */
	void OnMapsUpdated(const TUniquePtr<UE::Geometry::FMeshMapBaker>& NewResult);


	/**
	 * Update the preview material parameters for a given a result index.
	 * @param PreviewMapType EBakeMapType to preview
	 */
	void UpdatePreview(EBakeMapType PreviewMapType);
	

	/**
	 * Updates a tool property set's MapPreviewNamesList from the list of
	 * active map types. Also updates the MapPreview property if the current
	 * preview option is no longer available.
	 *
	 * @param Properties the UInteractiveToolPropertySet to update.
	 */
	template <typename PropertySet>
	void UpdatePreviewNames(PropertySet& Properties);


	/**
	 * Updates a tool property set's UVLayerNamesList from the list of UV layers
	 * on a given mesh. Also updates the UVLayer property if the current UV layer
	 * is no longer available.
	 *
	 * @param Properties the UInteractiveToolPropertySet to update.
	 * @param Mesh the mesh to query
	 */
	template <typename PropertySet>
	static void UpdateUVLayerNames(PropertySet& Properties, const FDynamicMesh3& Mesh);


	//
	// Analytics
	//
	struct FBakeAnalytics
	{
		double TotalBakeDuration = 0.0;
		double WriteToImageDuration = 0.0;
		double WriteToGutterDuration = 0.0;
		int64 NumBakedPixels = 0;
		int64 NumGutterPixels = 0;

		struct FMeshSettings
		{
			int32 NumTargetMeshTris = 0;
			int32 NumDetailMesh = 0;
			int64 NumDetailMeshTris = 0;
		};
		FMeshSettings MeshSettings;

		FBakeCacheSettings BakeSettings;
		FOcclusionMapSettings OcclusionSettings;
		FCurvatureMapSettings CurvatureSettings;
	};
	FBakeAnalytics BakeAnalytics;

	/**
	 * Computes the NumTargetMeshTris, NumDetailMesh and NumDetailMeshTris analytics.
	 * @param Data the mesh analytics data to compute
	 */
	virtual void GatherAnalytics(FBakeAnalytics::FMeshSettings& Data);

	/**
	 * Records bake timing and settings data for analytics.
	 * @param Result the result of the bake.
	 * @param Settings The bake settings used for the bake.
	 * @param Data the output bake analytics struct.
	 */
	static void GatherAnalytics(const UE::Geometry::FMeshMapBaker& Result,
								const FBakeCacheSettings& Settings,
								FBakeAnalytics& Data);

	/**
	 * Posts an analytics event using the given analytics struct.
	 * @param Data the bake analytics struct to output.
	 * @param EventName the name of the analytics event to output.
	 */
	static void RecordAnalytics(const FBakeAnalytics& Data, const FString& EventName);

	
	/**
	 * @return the analytics event name for this tool.
	 */
	virtual FString GetAnalyticsEventName() const
	{
		return TEXT("BakeTexture");
	}
	
	//
	// Utilities
	//
	const bool bPreferPlatformData = false;
	
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

	/**
	 * @param Properties the tool property set to validate the map type against.
	 * @param MapType the map type to validate.
	 * @return true if MapType was requested from the given PropertySet
	 */
	template <typename PropertySet>
	static bool IsRequestedMapType(PropertySet& Properties, EBakeMapType MapType);
	

	// empty maps are shown when nothing is computed
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyNormalMap;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapBlack;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyColorMapWhite;

	void InitializeEmptyMaps();
};


template <typename PropertySet>
void UBakeMeshAttributeMapsToolBase::OnMapTypesUpdated(PropertySet& Properties)
{
	const EBakeMapType BakeMapTypes = GetMapTypes(Properties->MapTypes);
	
	// Use the processed bitfield which may contain additional targets
	// (ex. AO if BentNormal was requested).
	CachedMaps.Empty();
	for (EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
	{
		if (MapType == EBakeMapType::Occlusion)
		{
			if ((bool)(BakeMapTypes & EBakeMapType::AmbientOcclusion))
			{
				CachedMaps.Add(EBakeMapType::AmbientOcclusion, nullptr);
			}
			if ((bool)(BakeMapTypes & EBakeMapType::BentNormal))
			{
				CachedMaps.Add(EBakeMapType::BentNormal, nullptr);
			}
		}
		else if( (bool)(BakeMapTypes & MapType) )
		{
			CachedMaps.Add(MapType, nullptr);
		}
	}

	// Initialize Properties->Result with requested MapTypes
	Properties->Result.Empty();
	for (const TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Map : CachedMaps)
	{
		// Only populate map types that were requested. Some map types like
		// AO may have only been added for preview of other types (ex. BentNormal)
		if (IsRequestedMapType(Properties, Map.Get<0>()))
		{
			Properties->Result.Add(Map.Get<0>(), nullptr);
		}
	}

	UpdatePreviewNames(Properties);
}


template <typename PropertySet>
void UBakeMeshAttributeMapsToolBase::UpdatePreviewNames(PropertySet& Properties)
{
	// Update our preview names list.
	Properties->MapPreviewNamesList.Reset();
	bool bFoundMapType = false;
	for (const TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Map : CachedMaps)
	{
		// Only populate map types that were requested. Some map types like
		// AO may have only been added for preview of other types (ex. BentNormal)
		if (IsRequestedMapType(Properties, Map.Get<0>()))
		{
			if (const FString* MapTypeName = MapTypeToNameMap.Find(Map.Get<0>()))
			{
				Properties->MapPreviewNamesList.Add(*MapTypeName);
				if (Properties->MapPreview == Properties->MapPreviewNamesList.Last())
				{
					bFoundMapType = true;
				}
			}
		}
	}
	if (!bFoundMapType)
	{
		Properties->MapPreview = Properties->MapPreviewNamesList.Num() > 0 ? Properties->MapPreviewNamesList[0] : TEXT("");
	}
}


template <typename PropertySet>
void UBakeMeshAttributeMapsToolBase::UpdateUVLayerNames(PropertySet& Properties, const FDynamicMesh3& Mesh)
{
	Properties->UVLayerNamesList.Reset();
	int32 FoundIndex = -1;
	for (int32 k = 0; k < Mesh.Attributes()->NumUVLayers(); ++k)
	{
		Properties->UVLayerNamesList.Add(FString::FromInt(k));
		if (Properties->UVLayer == Properties->UVLayerNamesList.Last())
		{
			FoundIndex = k;
		}
	}
	if (FoundIndex == -1)
	{
		Properties->UVLayer = Properties->UVLayerNamesList[0];
	}
}


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

template <typename PropertySet>
bool UBakeMeshAttributeMapsToolBase::IsRequestedMapType(PropertySet& Properties, EBakeMapType MapType)
{
	EBakeMapType SourceMapTypes = static_cast<EBakeMapType>(Properties->MapTypes);
	return static_cast<bool>(SourceMapTypes & MapType);
}






