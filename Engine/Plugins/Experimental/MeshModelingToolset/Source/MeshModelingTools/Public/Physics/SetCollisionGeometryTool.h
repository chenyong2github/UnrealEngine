// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "SphereTypes.h"
#include "OrientedBoxTypes.h"
#include "CapsuleTypes.h"
#include "Physics/CollisionPropertySets.h"
#include "SetCollisionGeometryTool.generated.h"

class UPreviewGeometry;
class FMeshSimpleShapeApproximation;


UCLASS()
class MESHMODELINGTOOLS_API USetCollisionGeometryToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
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
class MESHMODELINGTOOLS_API USetCollisionGeometryToolProperties : public UInteractiveToolPropertySet
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

	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "1", UIMax = "100", ClampMin = "4", ClampMax = "9999999", EditCondition = "bEnableMaxCount"))
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
class MESHMODELINGTOOLS_API USetCollisionGeometryTool : public UMultiSelectionTool
{
	GENERATED_BODY()
public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

protected:

	UPROPERTY()
	USetCollisionGeometryToolProperties* Settings = nullptr;


	UPROPERTY()
	UCollisionGeometryVisualizationProperties* VizSettings = nullptr;

	UPROPERTY()
	UPhysicsObjectToolPropertySet* CollisionProps;

	UPROPERTY()
	UMaterialInterface* LineMaterial = nullptr;

protected:
	UPROPERTY()
	UPreviewGeometry* PreviewGeom;

	TArray<int32> SourceObjectIndices;
	bool bSourcesHidden = false;

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

		FSphere3d DetectedSphere;
		FOrientedBox3d DetectedBox;
		FCapsule3d DetectedCapsule;
	};
	
	bool bInputMeshesValid = false;
	TArray<TSharedPtr<FDynamicMesh3>> InputMeshes;
	TArray<TSharedPtr<FDynamicMesh3>> CombinedInputMeshes;
	TArray<TSharedPtr<FDynamicMesh3>> SeparatedInputMeshes;
	TArray<TSharedPtr<FDynamicMesh3>> PerGroupInputMeshes;

	TSharedPtr<FMeshSimpleShapeApproximation> InputMeshesApproximator;
	TSharedPtr<FMeshSimpleShapeApproximation> CombinedInputMeshesApproximator;
	TSharedPtr<FMeshSimpleShapeApproximation> SeparatedMeshesApproximator;
	TSharedPtr<FMeshSimpleShapeApproximation> PerGroupMeshesApproximator;

	void PrecomputeInputMeshes();
	void InitializeDerivedMeshSet(
		const TArray<TSharedPtr<FDynamicMesh3>>& FromInputMeshes, 
		TArray<TSharedPtr<FDynamicMesh3>>& ToMeshes,
		TFunctionRef<bool(const FDynamicMesh3*, int32, int32)> TrisConnectedPredicate);
	TSharedPtr<FMeshSimpleShapeApproximation>& GetApproximator(ESetCollisionGeometryInputMode MeshSetMode);

	FTransform OrigTargetTransform;
	FVector TargetScale3D;

	bool bResultValid = false;
	TSharedPtr<FPhysicsDataCollection> InitialCollision;
	TSharedPtr<FPhysicsDataCollection> GeneratedCollision;

	void UpdateGeneratedCollision();
//	TSharedPtr<FPhysicsDataCollection> GenerateCollision_MinVolume();

	bool bVisualizationDirty = false;
	void UpdateVisualization();
};
