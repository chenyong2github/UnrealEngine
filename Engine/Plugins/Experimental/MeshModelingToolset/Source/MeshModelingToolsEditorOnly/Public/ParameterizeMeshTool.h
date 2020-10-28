// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h"
#include "Properties/MeshMaterialProperties.h"

#include "ParameterizeMeshTool.generated.h"


// predeclarations
class USimpleDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;



/** Material modes for MeshInspectorTool */
UENUM()
enum class EParameterizeMeshMaterialMode : uint8
{
	/** Input material */
	Default,

	/** Checkerboard material */
	Checkerboard,

	/** Override material */
	Override
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UParameterizeMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:

	IToolsContextAssetAPI* AssetAPI;
	bool bDoAutomaticGlobalUnwrap = false;

	UParameterizeMeshToolBuilder()
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};




UENUM()
enum class EUVUnwrapType
{
	/**  */
	MinStretch = 0,
	/** */
	ExpMap = 1,
	/** */
	Conformal = 2
};



UENUM()
enum class EUVIslandMode
{
	/**  */
	Auto = 0,
	/** */
	PolyGroups = 1,
	/** */
	ExistingUVs = 2
};



UENUM()
enum class EParameterizeMeshToolUVScaleMode
{
	/** No scaling is applied to UV islands */
	NoScaling,
	/** Scale UV islands such that they have constant relative area, relative to object bounds */
	NormalizeToBounds,
	/** Scale UV islands such that they have constant relative area, relative to world space */
	NormalizeToWorld
};




UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UParameterizeMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	//UPROPERTY(EditAnywhere, Category = Options)
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditConditionHides, HideEditConditionToggle, EditCondition = "bIsGlobalMode == false"))
	EUVIslandMode IslandMode = EUVIslandMode::PolyGroups;

	UPROPERTY(EditAnywhere, Category = Options, meta = (EditConditionHides, HideEditConditionToggle, EditCondition = "bIsGlobalMode == false"))
	EUVUnwrapType UnwrapType = EUVUnwrapType::ExpMap;

	/** Maximum amount of stretch, from none to any.  If zero stretch is specified each triangle will likey be its own chart */
	UPROPERTY(EditAnywhere, Category = Options, meta = (Default = "0.166", UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "bIsGlobalMode || UnwrapType == EUVUnwrapType::MinStretch"))
	float ChartStretch = 0.11f;

	/** Scaling applied to UV islands */
	UPROPERTY(EditAnywhere, Category = Options)
	EParameterizeMeshToolUVScaleMode UVScaleMode = EParameterizeMeshToolUVScaleMode::NormalizeToBounds;

	/** Scaling factor used for UV island normalization/scaling */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "UVScaleMode!=EParameterizeMeshToolUVScaleMode::NoScaling", UIMin = "0.001", UIMax = "10", ClampMin = "0.00001", ClampMax = "1000000.0") )
	float UVScale = 1.0;

	UPROPERTY(meta = (TransientToolProperty))
	bool bIsGlobalMode = false;
};





/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UParameterizeMeshTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UParameterizeMeshTool();

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);
	virtual void SetUseAutoGlobalParameterizationMode(bool bEnable);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;


	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:
	UPROPERTY()
	UParameterizeMeshToolProperties* Settings = nullptr;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;

protected:
	UPROPERTY()
	UMaterialInterface* DefaultMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* DisplayMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;
	
	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	UPROPERTY()
	bool bDoAutomaticGlobalUnwrap = false;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<FDynamicMesh3> InputMesh;

};
