// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MultiSelectionMeshEditingTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "SphereTypes.h"
#include "OrientedBoxTypes.h"
#include "CapsuleTypes.h"
#include "Physics/CollisionPropertySets.h"
#include "PropertySets/PolygroupLayersProperties.h"
#include "Polygroups/PolygroupSet.h"
#include "SetCollisionGeometryTool.generated.h"

class UPreviewGeometry;
PREDECLARE_GEOMETRY(class FMeshSimpleShapeApproximation)
PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMesh3);

UCLASS()
class MESHMODELINGTOOLSEXP_API USetCollisionGeometryToolBuilder : public UMultiSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UMultiSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};




UENUM()
enum class ESetCollisionGeometryInputMode
{
	CombineAll = 0,
	PerInputObject = 1,
	PerMeshComponent = 2,
	PerMeshGroup = 3
};


UENUM()
enum class ECollisionGeometryType
{
	KeepExisting = 0,
	AlignedBoxes = 1,
	OrientedBoxes = 2,
	MinimalSpheres = 3,
	Capsules = 4,
	ConvexHulls = 5,
	SweptHulls = 6,
	MinVolume = 10,

	None = 11
};



UENUM()
enum class EProjectedHullAxis
{
	X = 0,
	Y = 1,
	Z = 2,
	SmallestBoxDimension = 3,
	SmallestVolume = 4
};


UCLASS()
class MESHMODELINGTOOLSEXP_API USetCollisionGeometryToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Options)
	ECollisionGeometryType GeometryType = ECollisionGeometryType::AlignedBoxes;

	UPROPERTY(EditAnywhere, Category = Options)
	ESetCollisionGeometryInputMode InputMode = ESetCollisionGeometryInputMode::PerInputObject;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bUseWorldSpace = false;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bRemoveContained = true;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bEnableMaxCount = true;

	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "1", UIMax = "100", ClampMin = "1", ClampMax = "9999999", EditCondition = "bEnableMaxCount"))
	int32 MaxCount = 50;

	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	float MinThickness = 0.01;

	UPROPERTY(EditAnywhere, Category = AutoDetect)
	bool bDetectBoxes = true;

	UPROPERTY(EditAnywhere, Category = AutoDetect)
	bool bDetectSpheres = true;

	UPROPERTY(EditAnywhere, Category = AutoDetect)
	bool bDetectCapsules = true;

	UPROPERTY(EditAnywhere, Category = ConvexHulls, meta = (EditConditionHides, EditCondition = "GeometryType == ECollisionGeometryType::ConvexHulls"))
	bool bSimplifyHulls = true;

	UPROPERTY(EditAnywhere, Category = ConvexHulls, meta = (UIMin = "4", UIMax = "100", ClampMin = "4", ClampMax = "9999999",
		EditConditionHides, EditCondition = "GeometryType == ECollisionGeometryType::ConvexHulls"))
	int32 HullTargetFaceCount = 20;

	UPROPERTY(EditAnywhere, Category = SweptHulls, meta = (EditConditionHides, EditCondition = "GeometryType == ECollisionGeometryType::SweptHulls"))
	bool bSimplifyPolygons = true;

	UPROPERTY(EditAnywhere, Category = SweptHulls, meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100000",
		EditConditionHides, EditCondition = "GeometryType == ECollisionGeometryType::SweptHulls"))
	float HullTolerance = 0.1;

	UPROPERTY(EditAnywhere, Category = SweptHulls, meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100000",
		EditConditionHides, EditCondition = "GeometryType == ECollisionGeometryType::SweptHulls"))
	EProjectedHullAxis SweepAxis = EProjectedHullAxis::SmallestVolume;

	UPROPERTY(EditAnywhere, Category = OutputOptions)
	bool bAppendToExisting = false;

	UPROPERTY(EditAnywhere, Category = OutputOptions)
	ECollisionGeometryMode SetCollisionType = ECollisionGeometryMode::SimpleAndComplex;
};





/**
 * Mesh Inspector Tool for visualizing mesh information
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API USetCollisionGeometryTool : public UMultiSelectionMeshEditingTool
{
	GENERATED_BODY()
public:
	virtual void Setup() override;
	virtual void OnShutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

protected:

	UPROPERTY()
	TObjectPtr<USetCollisionGeometryToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UPolygroupLayersProperties> PolygroupLayerProperties = nullptr;

	UPROPERTY()
	TObjectPtr<UCollisionGeometryVisualizationProperties> VizSettings = nullptr;

	UPROPERTY()
	TObjectPtr<UPhysicsObjectToolPropertySet> CollisionProps;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> LineMaterial = nullptr;

protected:
	UPROPERTY()
	TObjectPtr<UPreviewGeometry> PreviewGeom;

	TArray<int32> SourceObjectIndices;
	bool bSourcesHidden = false;

	TArray<FDynamicMesh3> InitialSourceMeshes;

	void OnInputModeChanged();

	enum class EDetectedCollisionGeometry
	{
		None,
		Sphere = 2,
		Box = 4,
		Capsule = 8,
		Convex = 16
	};

	struct FSourceMesh
	{
		FDynamicMesh3 Mesh;

		EDetectedCollisionGeometry DetectedType = EDetectedCollisionGeometry::None;

		UE::Geometry::FSphere3d DetectedSphere;
		UE::Geometry::FOrientedBox3d DetectedBox;
		UE::Geometry::FCapsule3d DetectedCapsule;
	};
	
	bool bInputMeshesValid = false;
	TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>> InputMeshes;
	TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>> CombinedInputMeshes;
	TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>> SeparatedInputMeshes;
	TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>> PerGroupInputMeshes;

	TSharedPtr<UE::Geometry::FMeshSimpleShapeApproximation, ESPMode::ThreadSafe> InputMeshesApproximator;
	TSharedPtr<UE::Geometry::FMeshSimpleShapeApproximation, ESPMode::ThreadSafe> CombinedInputMeshesApproximator;
	TSharedPtr<UE::Geometry::FMeshSimpleShapeApproximation, ESPMode::ThreadSafe> SeparatedMeshesApproximator;
	TSharedPtr<UE::Geometry::FMeshSimpleShapeApproximation, ESPMode::ThreadSafe> PerGroupMeshesApproximator;

	void PrecomputeInputMeshes();
	void InitializeDerivedMeshSet(
		const TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>>& FromInputMeshes,
		TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>>& ToMeshes,
		TFunctionRef<bool(const FDynamicMesh3*, int32, int32)> TrisConnectedPredicate);
	TSharedPtr<UE::Geometry::FMeshSimpleShapeApproximation, ESPMode::ThreadSafe>& GetApproximator(ESetCollisionGeometryInputMode MeshSetMode);

	TUniquePtr<UE::Geometry::FPolygroupSet> ActiveGroupSet;
	void OnSelectedGroupLayerChanged();
	void UpdateActiveGroupLayer();

	FTransform OrigTargetTransform;
	FVector TargetScale3D;

	bool bResultValid = false;
	TSharedPtr<FPhysicsDataCollection, ESPMode::ThreadSafe> InitialCollision;
	TSharedPtr<FPhysicsDataCollection, ESPMode::ThreadSafe> GeneratedCollision;

	void UpdateGeneratedCollision();
//	TSharedPtr<FPhysicsDataCollection> GenerateCollision_MinVolume();

	bool bVisualizationDirty = false;
	void UpdateVisualization();
};
