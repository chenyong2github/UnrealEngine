// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "MultiSelectionTool.h"
#include "ProxyLODVolume.h"
#include "Properties/MeshStatisticsProperties.h"
#include "VoxelCSGMeshesTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UVoxelCSGMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI;

	UVoxelCSGMeshesToolBuilder()
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**  */
UENUM()
enum class EVoxelCSGOperation : uint8
{
	/** Subtracts the first object from the second */
	DifferenceAB = 0 UMETA(DisplayName = "A - B"),

	/** Subtracts the second object from the first */
	DifferenceBA = 1 UMETA(DisplayName = "B - A"),

	/** intersection of two objects */
	Intersect = 2 UMETA(DisplayName = "Intersect"),

	/** union of two objects */
	Union = 3 UMETA(DisplayName = "Union"),

};


/**
 * Standard properties of the Voxel CSG operation
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UVoxelCSGMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The type of operation  */
	UPROPERTY(EditAnywhere, Category = Options)
	EVoxelCSGOperation Operation;

	/** The size of the geometry bounding box major axis measured in voxels.*/
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "8", UIMax = "512", ClampMin = "8", ClampMax = "512"))
	int32 VoxelCount = 128;

	/** Remeshing adaptivity */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float MeshAdaptivity = 0.001f;

	/** Offset when remeshing, measured in voxels units */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "-2", UIMax = "2", ClampMin = "-2", ClampMax = "2"))
	float OffsetDistance = 0;

	/** Automatically simplify the result of voxel-based merge.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAutoSimplify = false;

	/** Remove the source Actors/Components when accepting results of tool.*/
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDeleteInputActors = true;
};




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UVoxelCSGMeshesTool : public UMultiSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	UVoxelCSGMeshesTool();

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
	UVoxelCSGMeshesToolProperties* CSGProps;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<TArray<IVoxelBasedCSG::FPlacedMesh>> InputMeshes;
	/** stash copies of the transforms and pointers to the meshes for consumption by the CSG Op*/
	void CacheInputMeshes();

	/** quickly generate a low-quality result for display while the actual result is being computed. */
	void CreateLowQualityPreview();

	void GenerateAsset(const TUniquePtr<FDynamicMeshOpResult>& Result);
};
