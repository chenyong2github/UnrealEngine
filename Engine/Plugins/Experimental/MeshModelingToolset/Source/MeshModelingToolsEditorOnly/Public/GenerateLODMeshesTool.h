// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshOpPreviewHelpers.h"
#include "CleaningOps/SimplifyMeshOp.h"
#include "Properties/MeshStatisticsProperties.h"
#include "Properties/RemeshProperties.h"
#include "GenerateLODMeshesTool.generated.h"





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGenerateLODMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



USTRUCT()
struct MESHMODELINGTOOLSEDITORONLY_API FLODLevelGenerateSettings
{
	GENERATED_BODY()

	/** Simplification Scheme  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyType SimplifierType = ESimplifyType::UE4Standard;

	/** Simplification Target Type  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyTargetType TargetMode;

	/** Target percentage */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100", EditConditionHides, EditCondition = "TargetMode == ESimplifyTargetType::Percentage"))
	int32 TargetPercentage;

	/** Target vertex/triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "4", UIMax = "10000", ClampMin = "1", ClampMax = "9999999999", EditConditionHides, EditCondition = "TargetMode == ESimplifyTargetType::TriangleCount || TargetMode == ESimplifyTargetType::VertexCount"))
	int32 TargetCount;

	/** Target vertex/triangle count */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Options)
	bool bReproject = false;

	UPROPERTY(VisibleAnywhere, Category = Options)
	FString Result;

	bool operator!=(const FLODLevelGenerateSettings& Other)
	{
		return SimplifierType != Other.SimplifierType || TargetMode != Other.TargetMode || TargetPercentage != Other.TargetPercentage || TargetCount != Other.TargetCount || bReproject != Other.bReproject;
	}
};



/**
 * Standard properties of the Simplify operation
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGenerateLODMeshesToolProperties : public UMeshConstraintProperties
{
	GENERATED_BODY()
public:
	UGenerateLODMeshesToolProperties();

	/** Simplification Target Type  */
	//UPROPERTY(EditAnywhere, Category = Options)
	UPROPERTY()
	ESimplifyTargetType TargetMode = ESimplifyTargetType::Percentage;

	/** Simplification Scheme  */
	//UPROPERTY(EditAnywhere, Category = Options)
	UPROPERTY()
	ESimplifyType SimplifierType = ESimplifyType::UE4Standard;

	/** Output LOD Assets will be numbered starting at this number */
	UPROPERTY(EditAnywhere, Category = Options)
	int NameIndexBase = 0;

	/** Target percentage of original triangle count */
	//UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100", EditCondition = "TargetMode == ESimplifyTargetType::Percentage"))
	UPROPERTY()
	int TargetPercentage = 50;

	/** Target edge length */
	//UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "3.0", UIMax = "10.0", ClampMin = "0.001", ClampMax = "1000.0", EditCondition = "TargetMode == ESimplifyTargetType::EdgeLength && SimplifierType != ESimplifyType::UE4Standard"))
	UPROPERTY()
	float TargetEdgeLength;

	/** Target triangle/vertex count */
	//UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "4", UIMax = "10000", ClampMin = "1", ClampMax = "9999999999", EditCondition = "TargetMode == ESimplifyTargetType::TriangleCount"))
	UPROPERTY()
	int TargetCount = 1000;

	/** If true, UVs and Normals are discarded  */
	//UPROPERTY(EditAnywhere, Category = Options)
	UPROPERTY()
	bool bDiscardAttributes = false;

	/** If true, display wireframe */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowWireframe = false;

	/** Display colors corresponding to the mesh's polygon groups */
	//UPROPERTY(EditAnywhere, Category = Display)
	UPROPERTY()
	bool bShowGroupColors = false;

	/** Enable projection back to input mesh */
	//UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	UPROPERTY()
	bool bReproject;

	UPROPERTY(EditAnywhere, Category = Options)
	TArray<FLODLevelGenerateSettings> LODLevels;

};



class FGenerateLODOperatorFactory : public IDynamicMeshOperatorFactory
{
public:
	TWeakObjectPtr<UGenerateLODMeshesTool> ParentTool;
	FLODLevelGenerateSettings LODSettings;
	FTransform UseTransform;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;
};



/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGenerateLODMeshesTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;



private:
	UPROPERTY()
	UGenerateLODMeshesToolProperties* SimplifyProperties;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;

	TArray<TUniquePtr<FGenerateLODOperatorFactory>> PreviewFactories;
	friend class FGenerateLODOperatorFactory;

	void UpdateNumPreviews();
	void InvalidateAllPreviews();

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<FMeshDescription> OriginalMeshDescription;
	// Dynamic Mesh versions precomputed in Setup (rather than recomputed for every simplify op)
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3> OriginalMeshSpatial;

	TArray<FLODLevelGenerateSettings> CachedLODLevels;

	FAxisAlignedBox3d WorldBounds;

	void GenerateAssets();
	void UpdateVisualization();

	void OnPreviewUpdated(UMeshOpPreviewWithBackgroundCompute*);
};
