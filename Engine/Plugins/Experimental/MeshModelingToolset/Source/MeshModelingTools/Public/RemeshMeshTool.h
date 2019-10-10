// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Properties/MeshStatisticsProperties.h"
#include "RemeshMeshTool.generated.h"

// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;

/** Mesh paint color view modes (somewhat maps to EVertexColorViewMode engine enum.) */
UENUM()
enum class ERemeshSmoothingType : uint8
{
	/** Uniform Smoothing */
	Uniform = 0 UMETA(DisplayName = "Uniform"),

	/** Cotangent Smoothing */
	Cotangent = 1 UMETA(DisplayName = "Shape Preserving"),

	/** Mean Value Smoothing */
	MeanValue = 2 UMETA(DisplayName = "Mixed")
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/**
 * Standard properties of the Remesh operation
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	URemeshMeshToolProperties();

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bUseTargetEdgeLength == false"))
	int TargetTriangleCount;


	/** Smoothing speed */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingSpeed;

	/** Smoothing type */
	UPROPERTY(EditAnywhere, Category = Options)
	ERemeshSmoothingType SmoothingType;

	/** Number of Remeshing passes */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "50", ClampMin = "0", ClampMax = "1000"))
	int RemeshIterations;


	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes;

	/** If true, sharp edges are preserved  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bPreserveSharpEdges;


	/** If true, the target count is ignored and the target edge length is used directly */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bUseTargetEdgeLength;

	/** Target edge length */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay, meta = (NoSpinbox = "true", EditCondition = "bUseTargetEdgeLength == true"))
	float TargetEdgeLength;


	/** Enable edge flips */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bFlips;

	/** Enable edge splits */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bSplits;

	/** Enable edge collapses */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bCollapses;

	/** Enable projection back to input mesh */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bReproject;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bPreventNormalFlips;

};


/**
 * Simple Mesh Remeshing Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API URemeshMeshTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	URemeshMeshTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

public:

	UPROPERTY()
	URemeshMeshToolProperties* BasicProperties;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;


protected:
	USimpleDynamicMeshComponent* DynamicMeshComponent;
	FDynamicMesh3 OriginalMesh;
	FDynamicMeshAABBTree3 OriginalMeshSpatial;
	bool bResultValid;
	void UpdateResult();

	double InitialMeshArea;

	double CalculateTargetEdgeLength(int TargetTriCount);

	void ComputeRemeshing();
};
