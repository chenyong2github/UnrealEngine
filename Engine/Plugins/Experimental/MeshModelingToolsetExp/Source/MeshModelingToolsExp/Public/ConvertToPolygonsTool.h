// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "PreviewMesh.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "ConvertToPolygonsTool.generated.h"

// predeclaration
class UConvertToPolygonsTool;
class FConvertToPolygonsOp;
class UPreviewGeometry;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UConvertToPolygonsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()
public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState & SceneState) const override;
};




UENUM()
enum class EConvertToPolygonsMode
{
	/** Convert based on Angle Tolerance between Face Normals */
	FaceNormalDeviation UMETA(DisplayName = "Face Normal Deviation"),
	/** Create PolyGroups based on UV Islands */
	FromUVIslands  UMETA(DisplayName = "From UV Islands"),
	/** Create PolyGroups based on Hard Normal Seams */
	FromNormalSeams  UMETA(DisplayName = "From Hard Normal Seams"),
	/** Create Polygroups based on Connected Triangles */
	FromConnectedTris UMETA(DisplayName = "From Connected Tris"),
	/** Create Polygroups centered on well-spaced sample points, approximating a surface Voronoi diagram */
	FromFurthestPointSampling UMETA(DisplayName = "Furthest Point Sampling")
};



UCLASS()
class MESHMODELINGTOOLSEXP_API UConvertToPolygonsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Strategy to use to group triangles */
	UPROPERTY(EditAnywhere, Category = PolyGroups)
	EConvertToPolygonsMode ConversionMode = EConvertToPolygonsMode::FaceNormalDeviation;

	/** Tolerance for planarity */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (UIMin = "0.001", UIMax = "60.0", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation", EditConditionHides))
	float AngleTolerance = 0.1f;

	/** Furthest-Point Sample count, approximately this number of polygroups will be generated */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (UIMin = "1", UIMax = "100", ClampMin = "1", ClampMax = "10000", EditCondition = "ConversionMode == EConvertToPolygonsMode::FromFurthestPointSampling", EditConditionHides))
	int32 NumPoints = 100;

	/** If enabled, then furthest-point sampling happens with respect to existing Polygroups, ie the existing groups are further subdivided */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (EditCondition = "ConversionMode == EConvertToPolygonsMode::FromFurthestPointSampling", EditConditionHides))
	bool bSplitExisting = false;

	/** If true, region-growing in Sampling modes will be controlled by face normals, resulting in regions with borders that are more-aligned with curvature ridges */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (EditCondition = "ConversionMode == EConvertToPolygonsMode::FromFurthestPointSampling", EditConditionHides))
	bool bNormalWeighted = true;

	/** This parameter modulates the effect of normal weighting during region-growing */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (UIMin = "0.1", UIMax = "2.0", ClampMin = "0.01", ClampMax = "100.0", EditCondition = "ConversionMode == EConvertToPolygonsMode::FromFurthestPointSampling", EditConditionHides))
	float NormalWeighting = 1.0f;


	/** group filtering */
	UPROPERTY(EditAnywhere, Category = Filtering, meta = (UIMin = "1", UIMax = "100", ClampMin = "1", ClampMax = "10000"))
	int32 MinGroupSize = 2;


	/** If true, normals are recomputed per-group, with hard edges at group boundaries */
	UPROPERTY(EditAnywhere, Category = Output)
	bool bCalculateNormals = false;
	
	/** Display each group with a different auto-generated color */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowGroupColors = true;
};

UCLASS()
class MESHMODELINGTOOLSEXP_API UConvertToPolygonsOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	TObjectPtr<UConvertToPolygonsTool> ConvertToPolygonsTool;  // back pointer
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UConvertToPolygonsTool : public USingleSelectionMeshEditingTool
{
	GENERATED_BODY()

public:
	UConvertToPolygonsTool();

	virtual void Setup() override;
	virtual void OnShutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// update parameters in ConvertToPolygonsOp based on current Settings
	void UpdateOpParameters(FConvertToPolygonsOp& ConvertToPolygonsOp) const;

protected:
	
	void OnSettingsModified();

protected:
	UPROPERTY()
	TObjectPtr<UConvertToPolygonsToolProperties> Settings;

	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> PreviewCompute = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewGeometry> PreviewGeometry = nullptr;

protected:
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> OriginalDynamicMesh;

	// for visualization
	TArray<int> PolygonEdges;
	
	void UpdateVisualization();
};
