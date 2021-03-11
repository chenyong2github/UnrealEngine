// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshOpPreviewHelpers.h"
#include "CleaningOps/RemeshMeshOp.h"
#include "Properties/MeshStatisticsProperties.h"
#include "Properties/RemeshProperties.h"
#include "UObject/UObjectGlobals.h"
#include "RemeshMeshTool.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	/** 
	 * Return true if we have one object selected. URemeshMeshTool is a UMultiSelectionTool, however we currently 
	 * only ever apply it to a single mesh. (See comment at URemeshMeshTool definition below.)
	 */
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

/**
 * Standard properties of the Remesh operation
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshToolProperties : public URemeshProperties
{
	GENERATED_BODY()

public:
	URemeshMeshToolProperties();

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (EditCondition = "bUseTargetEdgeLength == false"))
	int TargetTriangleCount;

	/** Smoothing type */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	ERemeshSmoothingType SmoothingType;

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	bool bDiscardAttributes;

	/** If true, display wireframe */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowWireframe = true;

	/** Display colors corresponding to the mesh's polygon groups */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowGroupColors = false;

	/** Display constrained edges */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowConstraintEdges = false;


	/** Remeshing type */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	ERemeshType RemeshType;

	/** Number of Remeshing passes */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay, meta = (EditCondition = "RemeshType == ERemeshType::FullPass", UIMin = "0", UIMax = "50", ClampMin = "0", ClampMax = "1000"))
	int RemeshIterations;

	/** Maximum number of Remeshing passes, for Remeshers that have convergence criteria */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay, meta = (EditCondition = "RemeshType != ERemeshType::FullPass", UIMin = "0", UIMax = "200", ClampMin = "0", ClampMax = "200"))
	int MaxRemeshIterations;

	/** For NormalFlowRemesher: extra iterations of normal flow with no remeshing */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay, meta = (EditCondition = "RemeshType == ERemeshType::NormalFlow", UIMin = "0", UIMax = "200", ClampMin = "0", ClampMax = "200"))
	int ExtraProjectionIterations;

	/** If true, the target count is ignored and the target edge length is used directly */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bUseTargetEdgeLength;

	/** Target edge length */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay, meta = (NoSpinbox = "true", EditCondition = "bUseTargetEdgeLength == true"))
	float TargetEdgeLength;

	/** Enable projection back to input mesh */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bReproject;
};


/**
 * Simple Mesh Remeshing Tool
 *
 * Note this is a subclass of UMultiSelectionTool, however we currently only ever apply it to one mesh at a time. The
 * function URemeshMeshToolBuilder::CanBuildTool will return true only when a single mesh is selected, and the tool will
 * only be applied to the first mesh in the selection list. The reason we inherit from UMultiSelectionTool is so 
 * that subclasses of this class can work with multiple meshes (see, for example, UProjectToTargetTool.)
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshTool : public UMultiSelectionTool, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	URemeshMeshTool(const FObjectInitializer&);

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	URemeshMeshToolProperties* BasicProperties;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

protected:

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> OriginalMeshSpatial;
	double InitialMeshArea;

	double CalculateTargetEdgeLength(int TargetTriCount);
	void GenerateAsset(const FDynamicMeshOpResult& Result);
	void UpdateVisualization();
};
