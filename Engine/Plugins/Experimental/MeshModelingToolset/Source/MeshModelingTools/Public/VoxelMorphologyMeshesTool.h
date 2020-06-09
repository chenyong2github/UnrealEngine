// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "BaseTools/SingleClickTool.h"
#include "Properties/OnAcceptProperties.h"
#include "Properties/VoxelProperties.h"

#include "CompositionOps/VoxelMorphologyMeshesOp.h"

#include "VoxelMorphologyMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;
class UTransformGizmo;
class UTransformProxy;


UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Properties of the morphology tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Show UI to allow changing translation, rotation and scale of input meshes */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowTransformUI = false;

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping, meta = (EditCondition = "bShowTransformUI == true"))
	bool bSnapToWorldGrid = false;

	UPROPERTY(EditAnywhere, Category = Options)
	EMorphologyOperation Operation = EMorphologyOperation::Dilate;

	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000"))
	double Distance = 5;

	/** Solidify the input mesh(es) before processing, fixing results for inputs with holes and/or self-intersections */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bSolidifyInput = false;
};



/**
 * Morphology tool -- dilate, contract, close, open operations on the input shape
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	UVoxelMorphologyMeshesTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UPROPERTY()
	UVoxelMorphologyMeshesToolProperties* MorphologyProperties;

	UPROPERTY()
	UVoxelProperties* VoxProperties;

	UPROPERTY()
	UOnAcceptHandleSourcesProperties* HandleSourcesProperties;

	UPROPERTY()
	TArray<UTransformProxy*> TransformProxies;

	UPROPERTY()
	TArray<UTransformGizmo*> TransformGizmos;

	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void SetupPreview();
	void SetTransformGizmos();
	void UpdateGizmoVisibility();

	void GenerateAsset(const FDynamicMeshOpResult& Result);

	void UpdateVisualization();
};
