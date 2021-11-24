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
#include "BakeMeshAttributeTool.h"
#include "BakeMeshAttributeMapsToolBase.generated.h"

/**
* Bake maps enums
*/


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
enum class EBakeTextureBitDepth
{
	ChannelBits8 UMETA(DisplayName = "8 bits/channel"),
	ChannelBits16 UMETA(DisplayName = "16 bits/channel")
};


UENUM()
enum class EBakeTextureSamplesPerPixel
{
	Sample1 = 1 UMETA(DisplayName = "1"),
	Sample4 = 4 UMETA(DisplayName = "4"),
	Sample16 = 16 UMETA(DisplayName = "16"),
	Sample64 = 64 UMETA(DisplayName = "64"),
	Sample256 = 256 UMETA(DisplayName = "256")
};


/**
 * Base Bake Maps tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeMapsToolBase : public UBakeMeshAttributeTool, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshMapBaker>
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsToolBase() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	// End UInteractiveTool interface

	// Begin IGenericDataOperatorFactory interface
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshMapBaker>> MakeNewOperator() override;
	// End IGenericDataOperatorFactory interface

	/**
	 * Process a bitfield into an EBakeMapType. This function
	 * may inject additional map types based on the enabled bits.
	 * For example, enabling AmbientOcclusion if BentNormal is
	 * active.
	 * @return An enumerated map type from a bitfield
	 */
	static EBakeMapType GetMapTypes(const int32& MapTypes);

protected:
	//
	// Tool property sets
	//
	UPROPERTY()
	TObjectPtr<UBakeVisualizationProperties> VisualizationProps;

	//
	// Preview mesh and materials
	//
	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BentNormalPreviewMaterial;

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

	//
	// Bake parameters
	//
	struct FBakeSettings
	{
		EBakeMapType SourceBakeMapTypes = EBakeMapType::None;
		EBakeMapType BakeMapTypes = EBakeMapType::None;
		FImageDimensions Dimensions;
		EBakeTextureBitDepth BitDepth = EBakeTextureBitDepth::ChannelBits8;
		int32 TargetUVLayer = 0;
		int32 DetailTimestamp = 0;
		float ProjectionDistance = 3.0;
		int32 SamplesPerPixel = 1;
		bool bProjectionInWorldSpace = false;

		bool operator==(const FBakeSettings& Other) const
		{
			return BakeMapTypes == Other.BakeMapTypes && Dimensions == Other.Dimensions &&
				TargetUVLayer == Other.TargetUVLayer && DetailTimestamp == Other.DetailTimestamp &&
				ProjectionDistance == Other.ProjectionDistance && SamplesPerPixel == Other.SamplesPerPixel &&
				BitDepth == Other.BitDepth && SourceBakeMapTypes == Other.SourceBakeMapTypes &&
				bProjectionInWorldSpace == Other.bProjectionInWorldSpace;
		}
	};
	FBakeSettings CachedBakeSettings;

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
	 * Update the preview material parameters for a given Bake type
	 * display name.
	 * @param Properties Properties containing the display name of a Bake type to preview.
	 */
	template <typename PropertySet>
	void UpdatePreview(PropertySet& Properties);


	/**
	 * Update the preview material parameters for a given a Bake type.
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


	//
	// Analytics
	//
	struct FBakeAnalytics
	{
		double TotalBakeDuration = 0.0;
		double WriteToImageDuration = 0.0;
		double WriteToGutterDuration = 0.0;
		int64 NumSamplePixels = 0;
		int64 NumGutterPixels = 0;

		struct FMeshSettings
		{
			int32 NumTargetMeshTris = 0;
			int32 NumDetailMesh = 0;
			int64 NumDetailMeshTris = 0;
		};
		FMeshSettings MeshSettings;

		FBakeSettings BakeSettings;
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
								const FBakeSettings& Settings,
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
	
	/** @return the Texture2D type for a given map type */
	static UE::Geometry::FTexture2DBuilder::ETextureType GetTextureType(EBakeMapType MapType, EBakeTextureBitDepth MapFormat);

	/** @return the texture name given a base name and map type */
	static void GetTextureName(EBakeMapType MapType, const FString& BaseName, FString& TexName);

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
void UBakeMeshAttributeMapsToolBase::UpdatePreview(PropertySet& Properties)
{
	const FString& PreviewDisplayName = Properties->MapPreview;
	if (const FString* PreviewNameString = Properties->MapPreviewNamesMap.Find(PreviewDisplayName))
	{
		const int64 PreviewValue = StaticEnum<EBakeMapType>()->GetValueByNameString(*PreviewNameString);
		if (PreviewValue != INDEX_NONE)
		{
			UpdatePreview(static_cast<EBakeMapType>(PreviewValue));
		}
	}
}


template <typename PropertySet>
void UBakeMeshAttributeMapsToolBase::UpdatePreviewNames(PropertySet& Properties)
{
	// Update our preview names list.
	Properties->MapPreviewNamesList.Reset();
	Properties->MapPreviewNamesMap.Reset();
	bool bFoundMapType = false;
	for (const TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Map : CachedMaps)
	{
		// Only populate map types that were requested. Some map types like
		// AO may have only been added for preview of other types (ex. BentNormal)
		if (IsRequestedMapType(Properties, Map.Get<0>()))
		{
			const UEnum* BakeTypeEnum = StaticEnum<EBakeMapType>();
			const int64 BakeEnumValue = static_cast<int64>(Map.Get<0>());
			const FString BakeTypeDisplayName = BakeTypeEnum->GetDisplayNameTextByValue(BakeEnumValue).ToString();
			const FString BakeTypeNameString = BakeTypeEnum->GetNameStringByValue(BakeEnumValue);
			Properties->MapPreviewNamesList.Add(BakeTypeDisplayName);
			Properties->MapPreviewNamesMap.Emplace(BakeTypeDisplayName, BakeTypeNameString);
			if (Properties->MapPreview == Properties->MapPreviewNamesList.Last())
			{
				bFoundMapType = true;
			}
		}
	}
	if (!bFoundMapType)
	{
		Properties->MapPreview = Properties->MapPreviewNamesList.Num() > 0 ? Properties->MapPreviewNamesList[0] : TEXT("");
	}
}


template <typename PropertySet>
bool UBakeMeshAttributeMapsToolBase::IsRequestedMapType(PropertySet& Properties, EBakeMapType MapType)
{
	EBakeMapType SourceMapTypes = static_cast<EBakeMapType>(Properties->MapTypes);
	return static_cast<bool>(SourceMapTypes & MapType);
}






