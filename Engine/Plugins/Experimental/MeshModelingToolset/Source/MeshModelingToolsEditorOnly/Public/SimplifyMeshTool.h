// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Properties/MeshStatisticsProperties.h"
#include "SimplifyMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;




/**  */
UENUM()
enum class ESimplifyTargetType : uint8
{
	/** Percentage of input triangles */
	Percentage = 0 UMETA(DisplayName = "Percentage"),

	/** Target triangle count */
	TriangleCount = 1 UMETA(DisplayName = "Triangle Count"),

	/** Target edge length */
	EdgeLength = 2 UMETA(DisplayName = "Edge Length")
};

/**  */
UENUM()
enum class ESimplifyType : uint8
{
	/** Fastest. Standard quadric error metric. Will not simplify UV boundaries.*/
	QEM = 0 UMETA(DisplayName = "QEM"),

	/** Potentially higher quality. Takes the normal into account. Will not simplify UV bounaries. */
	Attribute = 1 UMETA(DisplayName = "Normal Aware"),

	/** Highest quality reduction.  Will simplify UV boundaries. */
	UE4Standard = 2 UMETA(DisplayName = "UE4 Standard"),
};




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USimplifyMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Simple Mesh Simplifying Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USimplifyMeshTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	USimplifyMeshTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	/** Simplification Target Type  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyTargetType TargetMode;

	/** Simplification Scheme  */
	UPROPERTY(EditAnywhere, Category = Options)
	ESimplifyType SimplifierType;

	/** Target percentage of original triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100"))
	int TargetPercentage;

	/** Target edge length */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "3.0", UIMax = "10.0", ClampMin = "0.001", ClampMax = "1000.0"))
	float TargetEdgeLength;

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "4", UIMax = "10000", ClampMin = "1", ClampMax = "9999999999"))
	int TargetCount;

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes;

	/** Enable projection back to input mesh */
	UPROPERTY(EditAnywhere, Category = Advanced)
	bool bReproject;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = Advanced)
	bool bPreventNormalFlips;

	UPROPERTY()
	UMeshStatisticsProperties* MeshStatisticsProperties;

protected:
	USimpleDynamicMeshComponent* DynamicMeshComponent;
	FDynamicMesh3 OriginalMesh;
	FDynamicMeshAABBTree3 OriginalMeshSpatial;
	bool bResultValid;
	void UpdateResult();

	void ComputeSimplifying();
};
