// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ProxyLODVolume.h"
#include "MergeMeshesTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMergeMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI;

	UMergeMeshesToolBuilder()
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Standard properties of the Merge Meshes operation
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMergeMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** The size of the geometry bounding box major axis measured in voxels.*/
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "16", UIMax = "2048", ClampMin = "16", ClampMax = "2048"))
	int32 VoxelCount = 128;

	/** Remeshing adaptivity */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Adaptivity = 0.001f;

	/** Offset when remeshing, measured in voxels units */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "-2", UIMax = "2", ClampMin = "-2", ClampMax = "2"))
	float IsoSurface = 0;

	/** Automatically simplify the result of voxel-based merge.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAutoSimplify = true;
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMergeMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UMergeMeshesTool();

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TSharedPtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:
	UPROPERTY()
	UMergeMeshesToolProperties* MergeProps;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<TArray<IVoxelBasedCSG::FPlacedMesh>> InputMeshes;
	void CacheInputMeshes();

	void GenerateAsset(const TUniquePtr<FDynamicMeshOpResult>& Result);
};
