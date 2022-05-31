// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/MultiSelectionMeshEditingTool.h"
#include "BakeMeshAttributeMapsToolBase.h"

// Render Capture algorithm includes
#include "Scene/SceneCapturePhotoSet.h"

#include "BakeRenderCaptureTool.generated.h"

class UTexture2D;

//
// Tool Result
//


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeRenderCaptureResults  : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> BaseColorMap;

	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> RoughnessMap;

	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> MetallicMap;

	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> SpecularMap;

	/** Packed Metallic/Roughness/Specular Map */
	UPROPERTY(VisibleAnywhere, Category = Results, meta = (DisplayName = "Packed MRS Map", TransientToolProperty))
	TObjectPtr<UTexture2D> PackedMRSMap;

	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> EmissiveMap;

	/** World space normal map */
	UPROPERTY(VisibleAnywhere, Category = Results, meta = (TransientToolProperty))
	TObjectPtr<UTexture2D> NormalMap;
};

struct FBakeRenderCaptureResultsBuilder
{
	TUniquePtr<TImageBuilder<FVector4f>> RoughnessImage;
	TUniquePtr<TImageBuilder<FVector4f>> MetallicImage; 
	TUniquePtr<TImageBuilder<FVector4f>> SpecularImage; 
	TUniquePtr<TImageBuilder<FVector4f>> PackedMRSImage; 
	TUniquePtr<TImageBuilder<FVector4f>> EmissiveImage;
	TUniquePtr<TImageBuilder<FVector4f>> ColorImage;
	TUniquePtr<TImageBuilder<FVector4f>> NormalImage; // Tangent-space normal map
};

//
// Tool Builder
//



UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeRenderCaptureToolBuilder : public UMultiSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UMultiSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};



//
// Tool Properties
//


UCLASS()
class MESHMODELINGTOOLSEXP_API URenderCaptureProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, DisplayName="Render Capture Resolution", meta = (ClampMin = "1", UIMin= "1"))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution512;

	// Whether to generate a texture for the World Normal property
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere)
	bool bNormalMap = true;

	// Whether to generate a texture for the Metallic property
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, meta=(EditCondition="bPackedMRSMap == false"))
	bool bMetallicMap = true;

	// Whether to generate a texture for the Roughness property
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, meta=(EditCondition="bPackedMRSMap == false"))
	bool bRoughnessMap = true;

	// Whether to generate a texture for the Specular property
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, meta=(EditCondition="bPackedMRSMap == false"))
	bool bSpecularMap = true;
	
	// Whether to generate a packed texture with Metallic, Roughness and Specular properties
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, DisplayName="Packed MRS Map")
	bool bPackedMRSMap = true;

	// Whether to generate a texture for the Emissive property
	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere)
	bool bEmissiveMap = true;

	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, AdvancedDisplay, DisplayName="Field of View", meta = (ClampMin = "5.0", ClampMax = "160.0"))
	float CaptureFieldOfView = 30.0f;

	UPROPERTY(Category = RenderCaptureOptions, EditAnywhere, AdvancedDisplay, meta = (ClampMin = "0.001", ClampMax = "1000.0"))
	float NearPlaneDist = 1.0f;

	bool operator==(const URenderCaptureProperties& Other) const
	{
		return Resolution == Other.Resolution
			&& bNormalMap == Other.bNormalMap
			&& bMetallicMap == Other.bMetallicMap
			&& bRoughnessMap == Other.bRoughnessMap
			&& bSpecularMap == Other.bSpecularMap
			&& bPackedMRSMap == Other.bPackedMRSMap
			&& bEmissiveMap == Other.bEmissiveMap
			&& CaptureFieldOfView == Other.CaptureFieldOfView
			&& NearPlaneDist == Other.NearPlaneDist;
	}

	bool operator != (const URenderCaptureProperties& Other) const
	{
		return !(*this == Other);
	}
};

UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeRenderCaptureToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	
	/** The map type to preview */
	UPROPERTY(EditAnywhere, Category = BakeOutput, meta=(DisplayName="Preview Output Type", TransientToolProperty, GetOptions = GetMapPreviewNamesFunc))
	FString MapPreview;
	
	UFUNCTION()
	const TArray<FString>& GetMapPreviewNamesFunc()
	{
		return MapPreviewNamesList;
	}
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> MapPreviewNamesList;

	/** Number of samples per pixel */
	UPROPERTY(EditAnywhere, Category = BakeOutput)
	EBakeTextureSamplesPerPixel SamplesPerPixel = EBakeTextureSamplesPerPixel::Sample1;

	/* Size of generated textures */
	UPROPERTY(EditAnywhere, Category = BakeOutput, DisplayName="Results Resolution", meta=(ClampMin="1", UIMin="1"))
	EBakeTextureResolution TextureSize = EBakeTextureResolution::Resolution512;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeRenderCaptureInputToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Target mesh to sample to */
	UPROPERTY(VisibleAnywhere, Category = BakeInput, DisplayName = "Target Mesh", meta = (TransientToolProperty))
	TObjectPtr<UStaticMesh> TargetStaticMesh = nullptr;

	/** UV channel to use for the target mesh */
	UPROPERTY(EditAnywhere, Category = BakeInput, meta = (DisplayName = "Target Mesh UV Channel",
		GetOptions = GetTargetUVLayerNamesFunc, TransientToolProperty, NoResetToDefault))
	FString TargetUVLayer;

	UFUNCTION()
	const TArray<FString>& GetTargetUVLayerNamesFunc() const
	{
		return TargetUVLayerNamesList;
	}

	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> TargetUVLayerNamesList;
};


//
// Tool
//



UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeRenderCaptureTool : public UBakeMeshAttributeMapsToolBase
{
	GENERATED_BODY()

public:
	UBakeRenderCaptureTool() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void OnShutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;
	// End UInteractiveTool interface


protected:

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Actors;

	UPROPERTY()
	TObjectPtr<UBakeRenderCaptureToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<URenderCaptureProperties> RenderCaptureProperties;

	UPROPERTY()
	TObjectPtr<UBakeRenderCaptureInputToolProperties> InputMeshSettings;

	// The computed textures are displayed in the details panel and used in the preview material, they are written
	// out to assest on shutdown.
	UPROPERTY()
	TObjectPtr<UBakeRenderCaptureResults> ResultSettings;
	
protected:

	// Begin UBakeMeshAttributeMapsToolBase interface
	virtual void UpdateResult() override;
	virtual void UpdateVisualization() override;
	virtual void GatherAnalytics(FBakeAnalytics::FMeshSettings& Data) override;
	// End UBakeMeshAttributeMapsToolBase interface
	
	void InvalidateResults();

	class FComputeFactory : public UE::Geometry::IGenericDataOperatorFactory<FBakeRenderCaptureResultsBuilder>
	{
	public:
		UBakeRenderCaptureTool* Tool;

		// Begin IGenericDataOperatorFactory interface
		virtual TUniquePtr<UE::Geometry::TGenericDataOperator<FBakeRenderCaptureResultsBuilder>> MakeNewOperator() override;
		// End IGenericDataOperatorFactory interface
	};

	// TODO :PortRenderCaptureToolToNewBakingFramework
	// This tool has been ported to modeling mode after the new baking framework was implemented,
	// since there is quite a lot of code to move the plan is to get the tool working in modeling
	// mode using the old framework and then port it to the new one. This means that some aspects
	// of the UBakeMeshAttributeMapsToolBase cannot yet be used, so we work around these and point
	// out where we're going to need to do future @Refactor-ing. -- matija.kecman, 4 March 2022

	// :PortRenderCaptureToolToNewBakingFramework
	// When we use the new baking framework, we should be able to use:
	// - UBakeMeshAttributeMapsToolBase::Compute
	// - UBakeMeshAttributeMapsToolBase::InvalidateCompute
	// - UBakeMeshAttributeMapsToolBase::OnMapsUpdated
	// - UBakeMeshAttributeMapsToolBase::OnTick
 	FComputeFactory ComputeFactory;
	TUniquePtr<TGenericDataBackgroundCompute<FBakeRenderCaptureResultsBuilder>> ComputeRC = nullptr;
	void InvalidateComputeRC();
	void OnMapsUpdatedRC(const TUniquePtr<FBakeRenderCaptureResultsBuilder>& NewResult);
	
	/**
	 * Create texture assets from our result map of Texture2D
	 * @param SourceWorld the source world to define where the texture assets will be stored.
	 * @param SourceAsset if not null, result textures will be stored adjacent to this asset.
	 */
	void CreateTextureAssetsRC(UWorld* SourceWorld, UObject* SourceAsset);

	// empty maps are shown when nothing is computed
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyEmissiveMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyPackedMRSMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyRoughnessMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyMetallicMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> EmptySpecularMap;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialRC;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialPackedRC;
	void InitializePreviewMaterials();

	// TODO We currently need to compute this on the game thread because the implementation has checks for this
	TUniquePtr<UE::Geometry::FSceneCapturePhotoSet> SceneCapture;
	bool bFirstEverSceneCapture = true;

	// If the user cancels a scene capture before the computation completes then the settings which changed to invoke
	// the capture are reverted to these values
	UPROPERTY()
	TObjectPtr<URenderCaptureProperties> ComputedRenderCaptureProperties;

	// Analytics
	virtual FString GetAnalyticsEventName() const override
	{
		return TEXT("BakeRC");
	}
};