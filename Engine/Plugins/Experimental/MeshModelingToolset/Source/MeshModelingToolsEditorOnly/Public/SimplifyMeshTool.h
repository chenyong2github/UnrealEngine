// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

#include "SimplifyMeshTool.generated.h"





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USimplifyMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Standard properties of the Simplify operation
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USimplifyMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	USimplifyMeshToolProperties();

	/** Simplification Target Type  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyTargetType TargetMode;

	/** Simplification Scheme  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyType SimplifierType;

	/** Target percentage of original triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100", EditCondition = "TargetMode == ESimplifyTargetType::Percentage"))
	int TargetPercentage;

	/** Target edge length */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "3.0", UIMax = "10.0", ClampMin = "0.001", ClampMax = "1000.0", EditCondition = "TargetMode == ESimplifyTargetType::EdgeLength && SimplifierType != ESimplifyType::UE4Standard"))
	float TargetEdgeLength;

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "4", UIMax = "10000", ClampMin = "1", ClampMax = "9999999999", EditCondition = "TargetMode == ESimplifyTargetType::TriangleCount"))
	int TargetCount;

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes;

	/** Enable projection back to input mesh */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bReproject;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bPreventNormalFlips;

};




/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USimplifyMeshTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
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
	USimplifyMeshToolProperties* SimplifyProperties;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<FMeshDescription> OriginalMeshDescription;
	// Dynamic Mesh versions precomputed in Setup (rather than recomputed for every simplify op)
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3> OriginalMeshSpatial;

	void GenerateAsset(const FDynamicMeshOpResult& Result);
};
