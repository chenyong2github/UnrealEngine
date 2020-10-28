// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "CleaningOps/RemoveOccludedTrianglesOp.h"
#include "DynamicMesh3.h"
#include "MeshAdapter.h"
#include "BaseTools/SingleClickTool.h"
#include "RemoveOccludedTrianglesTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API URemoveOccludedTrianglesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


// these UIMode enums are versions of the enums in Operations/RemoveOccludedTriangles.h, w/ some removed & some renamed to be more user friendly

UENUM()
enum class EOcclusionTriangleSamplingUIMode : uint8
{
	// currently do not expose centroid-only option; it almost always looks bad
	Vertices,
	VerticesAndCentroids
};

UENUM()
enum class EOcclusionCalculationUIMode : uint8
{
	GeneralizedWindingNumber, // maps to using fast winding number approximation
	RaycastOcclusionSamples
};



/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API URemoveOccludedTrianglesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	URemoveOccludedTrianglesToolProperties();

	/** The method for deciding whether a triangle is occluded */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation)
	EOcclusionCalculationUIMode OcclusionTestMethod = EOcclusionCalculationUIMode::GeneralizedWindingNumber;

	/** Where to sample triangles to test occlusion */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation)
	EOcclusionTriangleSamplingUIMode TriangleSampling = EOcclusionTriangleSamplingUIMode::VerticesAndCentroids;

	/** The winding isovalue for GeneralizedWindingNumber mode */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation, meta = (UIMin = "-1", UIMax = "1", ClampMin = "-2", ClampMax = "2", EditCondition = "OcclusionTestMethod==EOcclusionCalculationUIMode::GeneralizedWindingNumber"))
	double WindingIsoValue = 0.5;

	/** For raycast-based occlusion tests, optionally add random ray direction to increase the accuracy of the visibility sampling */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000", EditCondition = "OcclusionTestMethod==EOcclusionCalculationUIMode::RaycastOcclusionSamples"))
	int AddRandomRays = 0;

	/** Optionally add random samples to each triangle (in addition to those from TriangleSampling) to increase the accuracy of the visibility sampling */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	int AddTriangleSamples = 0;

	/** If false, when multiple meshes are selected the meshes can occlude each other.  When true, we process each selected mesh independently and only consider self-occlusions. */
	UPROPERTY(EditAnywhere, Category = OcclusionCalculation)
	bool bOnlySelfOcclude = false;
};




/**
 * Advanced properties
 */
UCLASS()
class MESHMODELINGTOOLS_API URemoveOccludedTrianglesAdvancedProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	URemoveOccludedTrianglesAdvancedProperties();

	/** Amount to numerically 'nudge' occlusion sample query points away from the surface (to avoid e.g. all occlusion sample rays hitting the source triangle) */
	// probably not actually a good idea to expose this to the user
	//UPROPERTY(EditAnywhere, Category = NormalsTopology, meta = (UIMin = "0.000000001", UIMax = ".0001", ClampMin = "0.0", ClampMax = "0.01"))
	double NormalOffset = FMathd::ZeroTolerance;
};


/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLS_API URemoveOccludedTrianglesOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	URemoveOccludedTrianglesTool *Tool;

	int PreviewIdx;
};


/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API URemoveOccludedTrianglesTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	friend URemoveOccludedTrianglesOperatorFactory;

	URemoveOccludedTrianglesTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;


protected:
	UPROPERTY()
	URemoveOccludedTrianglesToolProperties* BasicProperties;

	UPROPERTY()
	URemoveOccludedTrianglesAdvancedProperties* AdvancedProperties;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;

	// When multiple meshes in the selection correspond to the same asset, only one needs a PreviewWithBackgroundCompute
	//  all others just get a plain PreviewMesh copy that is updated via OnMeshUpdated broadcast from the source Preview
	UPROPERTY()
	TArray<UPreviewMesh*> PreviewCopies;


protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	// pre-transformed combined all-mesh data (with mesh geometry data in just raw index buffers as that's all we need)
	TSharedPtr<IndexMeshWithAcceleration> CombinedMeshTrees;

	TArray<TArray<int32>> PreviewToCopyIdx;
	TArray<int32> PreviewToTargetIdx;
	TArray<int32> TargetToPreviewIdx;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	void SetupPreviews();

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
