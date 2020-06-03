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

#include "VoxelBlendMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;
class UTransformGizmo;
class UTransformProxy;


UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Properties of the blend operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Show UI to allow changing translation, rotation and scale of input meshes */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowTransformUI = true;

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping, meta = (EditCondition = "bShowTransformUI == true"))
	bool bSnapToWorldGrid = false;

	/** Blend power controls the shape of the blend between shapes */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "1", UIMax = "4", ClampMin = "1", ClampMax = "10"))
	double BlendPower = 2;

	/** Blend falloff controls the size of the blend region */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000"))
	double BlendFalloff = 10;
};



/**
 * Tool to smoothly blend meshes together
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	UVoxelBlendMeshesTool();

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
	UVoxelBlendMeshesToolProperties* BlendProperties;

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
