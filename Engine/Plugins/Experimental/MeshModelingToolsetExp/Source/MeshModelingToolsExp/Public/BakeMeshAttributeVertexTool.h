// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolQueryInterfaces.h" // for UInteractiveToolExclusiveToolAPI
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "PreviewMesh.h"
#include "Sampling/MeshVertexBaker.h"
#include "BakeMeshAttributeToolCommon.h"
#include "BakeMeshAttributeVertexTool.generated.h"

// predeclarations
class UMaterialInstanceDynamic;


/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeVertexToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


UENUM()
enum class EBakeVertexMode
{
	/* Bake vertex data to color */
	Color,
	/* Bake vertex data to individual color channels */
	PerChannel
};


UENUM()
enum class EBakeVertexTypeColor
{
	/* Sample tangent space normals from the detail mesh */
	TangentSpaceNormal,
	/* Sample ambient occlusion from the detail mesh */
	AmbientOcclusion,
	/* Sample normals skewed towards the least occluded direction from the detail mesh */
	BentNormal,
	/* Sample mesh curvatures from the detail mesh */
	Curvature,
	/* Sample a source texture from the detail mesh UVs */
	Texture,
	/* Sample object space normals from the detail mesh */
	ObjectSpaceNormal,
	/* Sample object space face normals from the detail mesh */
	FaceNormal,
	/* Sample bounding box relative positions from the detail mesh */
	Position,
	/* Sample material IDs as unique colors from the detail mesh */
	MaterialID UMETA(DisplayName="Material ID"),
	/* Sample a source texture per material ID on the detail mesh */
	MultiTexture
};


UENUM()
enum class EBakeVertexTypeChannel
{
	None,
	/* Sample ambient occlusion from the detail mesh */
	AmbientOcclusion,
	/* Sample mesh curvatures from the detail mesh */
	Curvature
};


UENUM()
enum class EBakeVertexChannel
{
	R,
	G,
	B,
	A,
	RGBA
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeVertexToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The bake types to generate */
	UPROPERTY(EditAnywhere, Category = BakeSettings)
	EBakeVertexMode VertexMode = EBakeVertexMode::Color;

	/** The vertex channel to preview */
	UPROPERTY(EditAnywhere, Category = BakeSettings, meta = (TransientToolProperty))
	EBakeVertexChannel VertexChannelPreview = EBakeVertexChannel::RGBA;

	/** Distance to search for the correspondence between the source and target meshes */
	UPROPERTY(EditAnywhere, Category = BakeSettings, meta = (ClampMin = "0.001"))
	float Thickness = 3.0;

	/** Compute target mesh to detail mesh correspondence in world space */
	UPROPERTY(EditAnywhere, Category = BakeSettings)
	bool bUseWorldSpace = false;

	/** Split vertex colors at normal seams */
	UPROPERTY(EditAnywhere, Category = BakeSettings)
	bool bSplitAtNormalSeams = false;

	/** Split vertex colors at UV seams */
	UPROPERTY(EditAnywhere, Category = BakeSettings)
	bool bSplitAtUVSeams = false;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeVertexToolColorProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The bake type to generate */
	UPROPERTY(EditAnywhere, Category = ColorBakeSettings)
	EBakeVertexTypeColor BakeType = EBakeVertexTypeColor::TangentSpaceNormal;
};


UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeVertexToolChannelProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The bake type to generate in the Red channel */
	UPROPERTY(EditAnywhere, Category = PerChannelBakeSettings)
	EBakeVertexTypeChannel BakeTypeR = EBakeVertexTypeChannel::None;

	/** The bake type to generate in the Green channel */
	UPROPERTY(EditAnywhere, Category = PerChannelBakeSettings)
	EBakeVertexTypeChannel BakeTypeG = EBakeVertexTypeChannel::None;

	/** The bake type to generate in the Blue channel */
	UPROPERTY(EditAnywhere, Category = PerChannelBakeSettings)
	EBakeVertexTypeChannel BakeTypeB = EBakeVertexTypeChannel::None;

	/** The bake type to generate in the Alpha channel */
	UPROPERTY(EditAnywhere, Category = PerChannelBakeSettings)
	EBakeVertexTypeChannel BakeTypeA = EBakeVertexTypeChannel::None;
};


/**
 * Vertex Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeVertexTool : public UMultiSelectionTool, public IInteractiveToolExclusiveToolAPI, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshVertexBaker>
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeVertexTool() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;
	// End UInteractiveTool interface

	// Begin IGenericDataOperatorFactory interface
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshVertexBaker>> MakeNewOperator() override;
	// End IGenericDataOperatorFactory interface

	void SetWorld(UWorld* World);

protected:
	UPROPERTY()
	TObjectPtr<UBakeMeshAttributeVertexToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<UBakeMeshAttributeVertexToolColorProperties> ColorSettings;

	UPROPERTY()
	TObjectPtr<UBakeMeshAttributeVertexToolChannelProperties> PerChannelSettings;

	UPROPERTY()
	TObjectPtr<UBakedOcclusionMapToolProperties> OcclusionSettings;

	UPROPERTY()
	TObjectPtr<UBakedCurvatureMapToolProperties> CurvatureSettings;

	UPROPERTY()
	TObjectPtr<UBakedTexture2DImageProperties> TextureSettings;

	UPROPERTY()
	TObjectPtr<UBakedMultiTexture2DImageProperties> MultiTextureSettings;

protected:
	friend class FMeshVertexBakerOp;
	
	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewAlphaMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WorkingPreviewMaterial;
	float SecondsBeforeWorkingMaterial = 0.75;

	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld = nullptr;

	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshVertexBaker>> Compute = nullptr;
	void OnResultUpdated(const TUniquePtr<UE::Geometry::FMeshVertexBaker>& NewResult);

	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UE::Geometry::FDynamicMesh3 BaseMesh;
	UE::Geometry::FDynamicMeshAABBTree3 BaseSpatial;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	int32 DetailMeshTimestamp = 0;
	bool bDetailMeshValid = false;
	void UpdateDetailMesh();

	bool bColorTopologyValid = false;
	bool bIsBakeToSelf = false;
	void UpdateOnModeChange();
	void UpdateVisualization();
	void UpdateColorTopology();
	void UpdateResult();

	const bool bPreferPlatformData = false;

	EBakeOpState OpState = EBakeOpState::Evaluate;

	struct FBakeSettings
	{
		EBakeVertexMode VertexMode = EBakeVertexMode::Color;
		EBakeVertexChannel VertexChannelPreview = EBakeVertexChannel::RGBA;
		float Thickness = 3.0;
		bool bUseWorldSpace = false;
		bool bSplitAtNormalSeams = false;
		bool bSplitAtUVSeams = false;

		bool operator==(const FBakeSettings& Other) const
		{
			return (VertexMode == Other.VertexMode && bUseWorldSpace == Other.bUseWorldSpace &&
				Thickness == Other.Thickness && bSplitAtNormalSeams == Other.bSplitAtNormalSeams &&
				bSplitAtUVSeams == Other.bSplitAtUVSeams);
		}
	};
	FBakeSettings CachedBakeSettings;

	struct FBakeColorSettings
	{
		EBakeVertexTypeColor BakeType = EBakeVertexTypeColor::TangentSpaceNormal;
		bool operator==(const FBakeColorSettings& Other) const
		{
			return (BakeType == Other.BakeType);
		}
	};
	FBakeColorSettings CachedColorSettings;

	struct FBakeChannelSettings
	{
		EBakeVertexTypeChannel BakeType[4] = { EBakeVertexTypeChannel::None, EBakeVertexTypeChannel::None, EBakeVertexTypeChannel::None, EBakeVertexTypeChannel::None };
		bool operator==(const FBakeChannelSettings& Other) const
		{
			return (BakeType[0] == Other.BakeType[0] && BakeType[1] == Other.BakeType[1] && BakeType[2] == Other.BakeType[2] && BakeType[3] == Other.BakeType[3]);
		}
	};
	FBakeChannelSettings CachedChannelSettings;

	//FNormalMapSettings CachedNormalMapSettings;
	EBakeOpState UpdateResult_Normal();

	FOcclusionMapSettings CachedOcclusionMapSettings;
	EBakeOpState UpdateResult_Occlusion();

	FCurvatureMapSettings CachedCurvatureMapSettings;
	EBakeOpState UpdateResult_Curvature();

	//FMeshPropertyMapSettings CachedMeshPropertyMapSettings;
	EBakeOpState UpdateResult_MeshProperty();

	TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> CachedTextureImage;
	FTexture2DImageSettings CachedTexture2DImageSettings;
	EBakeOpState UpdateResult_Texture2DImage();

	TMap<int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> CachedMultiTextures;
	EBakeOpState UpdateResult_MultiTexture();

	//
	// Analytics
	//
	struct FBakeAnalytics
	{
		double TotalBakeDuration = 0.0;

		struct FMeshSettings
		{
			int32 NumTargetMeshVerts = 0;
			int32 NumTargetMeshTris = 0;
			int32 NumDetailMesh = 0;
			int64 NumDetailMeshTris = 0;
		};
		FMeshSettings MeshSettings;

		FBakeSettings BakeSettings;
		FBakeColorSettings BakeColorSettings;
		FBakeChannelSettings BakeChannelSettings;
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
	static void GatherAnalytics(const UE::Geometry::FMeshVertexBaker& Result,
								const FBakeSettings& Settings,
								const FBakeColorSettings& ColorSettings,
								const FBakeChannelSettings& ChannelSettings,
								FBakeAnalytics& Data);

	/**
	 * Outputs an analytics event using the given analytics struct.
	 * @param Data the bake analytics struct to output.
	 * @param EventName the name of the analytics event to output.
	 */
	static void RecordAnalytics(const FBakeAnalytics& Data, const FString& EventName);
};

