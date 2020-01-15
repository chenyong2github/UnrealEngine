// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ProxyLODVolume.h"
#include "Properties/MeshStatisticsProperties.h"
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
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "8", UIMax = "1024", ClampMin = "8", ClampMax = "1024"))
	int32 VoxelCount = 128;

	/** Remeshing adaptivity, prior to optional simplification */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float MeshAdaptivity = 0.001f;

	/** Offset when remeshing, note large offsets with high voxels counts will be slow */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "-10", UIMax = "10", ClampMin = "-10", ClampMax = "10"))
	float OffsetDistance = 0;

	/** Automatically simplify the result of voxel-based merge.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAutoSimplify = false;

	/** Delete the source Actors/Components when accepting results of tool.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDeleteInputActors = true;
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

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:
	UPROPERTY()
	UMergeMeshesToolProperties* MergeProps;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;


	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<TArray<IVoxelBasedCSG::FPlacedMesh>> InputMeshes;
	/** stash copies of the transforms and pointers to the meshes for consumption by merge Op*/
	void CacheInputMeshes();

	/** quickly generate a low-quality result for display while the actual result is being computed. */
	void CreateLowQualityPreview();

	void GenerateAsset(const FDynamicMeshOpResult& Result);
};
