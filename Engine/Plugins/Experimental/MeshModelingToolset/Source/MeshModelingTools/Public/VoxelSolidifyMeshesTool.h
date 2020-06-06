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

#include "VoxelSolidifyMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;
class UTransformGizmo;
class UTransformProxy;


UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Properties of the solidify operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Show UI to allow changing translation, rotation and scale of input meshes */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowTransformUI = true;

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping, meta = (EditCondition = "bShowTransformUI == true"))
	bool bSnapToWorldGrid = false;

	/** Winding number threshold to determine what is consider inside the mesh */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.1", UIMax = ".9", ClampMin = "-10", ClampMax = "10"))
	double WindingThreshold = .5;

	/** How far we allow bounds of solid surface to go beyond the bounds of the original input surface before clamping / cutting the surface off */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "1000"))
	double ExtendBounds = 1;

	/** How many binary search steps to take when placing vertices on the surface */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "6", ClampMin = "0", ClampMax = "10"))
	int SurfaceSearchSteps = 4;

	/** Whether to fill at the border of the bounding box, if the surface extends beyond the voxel boundaries */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bSolidAtBoundaries = true;

	/** If true, treats mesh surfaces with open boundaries as having a fixed, user-defined thickness */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bMakeOffsetSurfaces = false;

	/** Thickness of offset surfaces */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000", EditCondition = "bMakeOffsetSurfaces == true"))
	double OffsetThickness = 5;

	

	
	
};



/**
 * Tool to take one or more meshes, possibly intersecting and possibly with holes, and create a single solid mesh with consistent inside/outside
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	UVoxelSolidifyMeshesTool();

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
	UVoxelSolidifyMeshesToolProperties* SolidifyProperties;

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
