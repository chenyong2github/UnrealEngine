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

#include "RecomputeUVsTool.generated.h"


// predeclarations
class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API URecomputeUVsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};



UENUM()
enum class ERecomputeUVsUnwrapType
{
	// Values must match UE::Geometry::ERecomputeUVsUnwrapType

	/** */
	ExpMap = 0,
	/** */
	Conformal = 1
};



UENUM()
enum class ERecomputeUVsIslandMode
{
	// Values must match UE::Geometry::ERecomputeUVsIslandMode

	/** */
	PolyGroups = 0,
	/** */
	ExistingUVs = 1
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
class MESHMODELINGTOOLS_API URecomputeUVsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Compute UVs")
	ERecomputeUVsIslandMode IslandMode = ERecomputeUVsIslandMode::PolyGroups;

	UPROPERTY(EditAnywhere, Category = "Compute UVs")
	ERecomputeUVsUnwrapType UnwrapType = ERecomputeUVsUnwrapType::Conformal;

	UPROPERTY(EditAnywhere, Category = "UV Layout")
	bool bAutoPack = true;

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
class MESHMODELINGTOOLS_API URecomputeUVsTool : public USingleSelectionMeshEditingTool, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

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

protected:
	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview = nullptr;

protected:
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;


	TSharedPtr<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe> ActiveGroupSet;
	void OnSelectedGroupLayerChanged();
	void UpdateActiveGroupLayer();
};
