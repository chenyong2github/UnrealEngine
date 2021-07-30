// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h"
#include "Properties/MeshMaterialProperties.h"
#include "Properties/MeshUVChannelProperties.h"
#include "PropertySets/PolygroupLayersProperties.h"
#include "Polygroups/PolygroupSet.h"
#include "Drawing/UVLayoutPreview.h"

#include "RecomputeUVsTool.generated.h"


// predeclarations
class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API URecomputeUVsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};



UENUM()
enum class ERecomputeUVsUnwrapType
{
	// Values must match UE::Geometry::ERecomputeUVsUnwrapType

	/** ExpMap UV Flattening Algorithm is very fast but has limited ability to control stretching/distortion */
	ExpMap = 0,
	/** Conformal UV Flattening algorithm is increasingly-expensive on large islands but better at distortion control */
	Conformal = 1
};



UENUM()
enum class ERecomputeUVsIslandMode
{
	// Values must match UE::Geometry::ERecomputeUVsIslandMode

	/** Use Active Mesh Polygroups Layer to define initial UV Islands */
	PolyGroups = 0,
	/** Use Existing UV Layer to define UV Islands (ie re-solve UV flattening based on existing UVs) */
	ExistingUVs = 1
};


UENUM()
enum class ERecomputeUVsToolOrientationMode
{
	/**  */
	None,
	/**  */
	MinBoxBounds
};


UENUM()
enum class ERecomputeUVsToolUVScaleMode
{
	/** No scaling is applied to UV islands */
	NoScaling,
	/** Scale UV islands such that they have constant relative area, relative to object bounds */
	NormalizeToBounds,
	/** Scale UV islands such that they have constant relative area, relative to world space */
	NormalizeToWorld
};


UCLASS()
class MESHMODELINGTOOLSEXP_API URecomputeUVsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** IslandMode determines which patches of triangles will be used as initial UV Islands */
	UPROPERTY(EditAnywhere, Category = "UV Generation")
	ERecomputeUVsIslandMode IslandMode = ERecomputeUVsIslandMode::PolyGroups;

	/** Which type of UV flattening algorithm to use */
	UPROPERTY(EditAnywhere, Category = "UV Generation", meta = (EditCondition = "bIslandMerging == false"))
	ERecomputeUVsUnwrapType UnwrapType = ERecomputeUVsUnwrapType::Conformal;

	/** Which type of automatic island orientation to use */
	UPROPERTY(EditAnywhere, Category = "UV Generation")
	ERecomputeUVsToolOrientationMode AutoRotation = ERecomputeUVsToolOrientationMode::MinBoxBounds;

	/** If Island Merging is enabled, then the initial UV islands will be merged into larger islands if it does not increase distortion/stretching beyond the limits below */
	UPROPERTY(EditAnywhere, Category = "UV Generation")
	bool bIslandMerging = false;

	/** Distortion/Stretching Threshold for island merging - larger values increase the allowable UV stretching */
	UPROPERTY(EditAnywhere, Category = "UV Generation", meta = (UIMin = "1.0", UIMax = "5.0", ClampMin = "1.0", EditCondition = "bIslandMerging == true"))
	float MergingThreshold = 1.5f;

	/** UV islands will not be merged if their average face normals deviate by larger than this amount */
	UPROPERTY(EditAnywhere, Category = "UV Generation", meta = (UIMin = "0.0", UIMax = "90.0", ClampMin = "0.0", ClampMax = "180.0", EditCondition = "bIslandMerging == true"))
	float MaxAngleDeviation = 45.0f;

	/** Number of smoothing steps to apply in the ExpMap UV Generation method (Smoothing slightly increases distortion but produces more stable results) */
	UPROPERTY(EditAnywhere, Category = "ExpMap UV Generator", meta = (UIMin = "0", UIMax = "25", ClampMin = "0", ClampMax = "1000", EditCondition = "UnwrapType == ERecomputeUVsUnwrapType::ExpMap || bIslandMerging == true" ))
	int SmoothingSteps = 5;

	/** Smoothing parameter, larger values result in faster smoothing in each step */
	UPROPERTY(EditAnywhere, Category = "ExpMap UV Generator", meta = (UIMin = "0", UIMax = "1.0", ClampMin = "0", ClampMax = "1.0", EditCondition = "UnwrapType == ERecomputeUVsUnwrapType::ExpMap || bIslandMerging == true"))
	float SmoothingAlpha = 0.25f;

	/** If enabled, result UVs are automatically packed into the standard UV 0-1 square */
	UPROPERTY(EditAnywhere, Category = "UV Layout")
	bool bAutoPack = true;

	/** Target texture resolution used for UV packing, which determines gutter size */
	UPROPERTY(EditAnywhere, Category = "UV Layout", meta = (UIMin = "64", UIMax = "2048", ClampMin = "2", ClampMax = "4096", EditCondition = bAutoPack))
	int TextureResolution = 1024;

	/** Scaling applied to UV islands after solve */
	UPROPERTY(EditAnywhere, Category = "Transform UVs", meta = (EditCondition = "bAutoPack == false"))
	ERecomputeUVsToolUVScaleMode UVScaleMode = ERecomputeUVsToolUVScaleMode::NormalizeToBounds;

	/** Scaling factor used for UV island normalization/scaling */
	UPROPERTY(EditAnywhere, Category = "Transform UVs", meta = (EditCondition = "UVScaleMode!=ERecomputeUVsToolUVScaleMode::NoScaling && bAutoPack == false", UIMin = "0.001", UIMax = "10", ClampMin = "0.00001", ClampMax = "1000000.0") )
	float UVScale = 1.0;
};





/**
 * URecomputeUVsTool Recomputes UVs based on existing segmentations of the mesh
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API URecomputeUVsTool : public USingleSelectionMeshEditingTool, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

protected:
	UPROPERTY()
	TObjectPtr<UMeshUVChannelProperties> UVChannelProperties = nullptr;

	UPROPERTY()
	TObjectPtr<URecomputeUVsToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UPolygroupLayersProperties> PolygroupLayerProperties = nullptr;

	UPROPERTY()
	TObjectPtr<UExistingMeshMaterialProperties> MaterialSettings = nullptr;

	UPROPERTY()
	bool bCreateUVLayoutViewOnSetup = true;

	UPROPERTY()
	TObjectPtr<UUVLayoutPreview> UVLayoutView = nullptr;


protected:
	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview = nullptr;

protected:
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;


	TSharedPtr<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe> ActiveGroupSet;
	void OnSelectedGroupLayerChanged();
	void UpdateActiveGroupLayer();

	void OnPreviewMeshUpdated();
};
