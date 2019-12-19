// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "Drawing/ToolDataVisualizer.h"
#include "Transforms/QuickAxisTranslater.h"
#include "Transforms/QuickAxisRotator.h"
#include "Changes/MeshVertexChange.h"
#include "GroupTopology.h"
#include "Spatial/GeometrySet3.h"
#include "Selection/GroupTopologySelector.h"
#include "Operations/GroupTopologyDeformer.h"
#include "MeshSolverUtilities/Private/LaplacianMeshSmoother.h"
#include "Curves/CurveFloat.h"
#include "ModelingOperators/Public/ModelingTaskTypes.h"
#include "EditMeshPolygonsTool.generated.h"

class FMeshVertexChangeBuilder;
class FGroupTopologyLaplacianDeformer;
class FDeformTask;
struct FConstraintData;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	UEditMeshPolygonsToolBuilder()
	{
	}

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/** Deformation strategies */
UENUM()
enum class EGroupTopologyDeformationStrategy : uint8
{
	/** Deforms the mesh using linear translations*/
	Linear UMETA(DisplayName = "Linear"),

	/** Deforms the mesh using laplacian deformation*/
	Laplacian UMETA(DisplayName = "Laplacian")
};

/** Laplacian weight schemes determine how we will look at the curvature at a given vertex in relation to its neighborhood*/
UENUM()
enum class EWeightScheme 
{
	Uniform				UMETA(DisplayName = "Uniform"),
	Umbrella			UMETA(DisplayName = "Umbrella"),
	Valence				UMETA(DisplayName = "Valence"),
	MeanValue			UMETA(DisplayName = "MeanValue"),
	Cotangent			UMETA(DisplayName = "Cotangent"),
	ClampedCotangent	UMETA(DisplayName = "ClampedCotangent")
};

/** The ELaplacianWeightScheme enum is the same..*/
static ELaplacianWeightScheme ConvertToLaplacianWeightScheme(const EWeightScheme WeightScheme)
{
	return static_cast<ELaplacianWeightScheme>(WeightScheme);
}

/** Modes for quick transformer */
UENUM()
enum class EQuickTransformerMode : uint8
{
	/** Translation along frame axes */
	AxisTranslation = 0 UMETA(DisplayName = "Translate"),

	/** Rotation around frame axes*/
	AxisRotation = 1 UMETA(DisplayName = "Rotate"),
};


UENUM()
enum class EPolygonGroupMode : uint8
{
	KeepInputPolygons						UMETA(DisplayName = "Edit Input Polygons"),
	RecomputePolygonsByAngleThreshold		UMETA(DisplayName = "Recompute Polygons Based on Angle Threshold"),
	PolygonsAreTriangles					UMETA(DisplayName = "Edit Triangles Directly")
};



UCLASS()
class MESHMODELINGTOOLS_API UPolyEditTransformProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPolyEditTransformProperties();

#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

	//Options

	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Deformation Type", ToolTip = "Select the type of deformation you wish to employ on a polygroup."))
	EGroupTopologyDeformationStrategy DeformationStrategy;

	UPROPERTY(EditAnywhere, Category = Options)
	EQuickTransformerMode TransformMode;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectFaces;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectEdges;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectVertices;

	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Show Triangle Mesh Wireframe"))
	bool bWireframe;

	UPROPERTY(EditAnywhere, Category = Options)
	EPolygonGroupMode PolygonMode;

	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition="PolygonMode == EPolygonGroupMode::RecomputePolygonsByAngleThreshold"))
	float PolygonGroupingAngleThreshold;

	//Laplacian Deformation Options, currently not exposed.

	UPROPERTY() 
	EWeightScheme SelectedWeightScheme = EWeightScheme::ClampedCotangent;

	UPROPERTY() 
	double HandleWeight = 1000.0;

	UPROPERTY()
	bool bPostFixHandles = false;
/**
	// How to add a weight curve
	UPROPERTY(EditAnywhere, Category = LaplacianOptions, meta = (EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian", DisplayName = "Localize Deformation", ToolTip = "When enabled, only the vertices in the polygroups immediately adjacent to the selected group will be affected by the deformation.\nWhen disabled, the deformer will solve for the curvature of the entire mesh (slower)"))
	bool bLocalizeDeformation{ true};

	UPROPERTY(EditAnywhere, Category = LaplacianOptions, meta = (EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian", DisplayName = "Apply Weight Attenuation Curve", ToolTip = "When enabled, the curve below will be used to calculate the weight at a given vertex based on distance from the handles"))
	bool bApplyAttenuationCurve{ false };
	
	FRichCurve DefaultFalloffCurve;

	UPROPERTY(EditAnywhere,  Category = LaplacianOptions, meta = ( EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian && bApplyAttenuationCurve", DisplayName = "Distance-Weight Attenuation Curve",UIMin = "1.0", UIMax = "1.0", ClampMin = "1.0", ClampMax = "1.0", ToolTip = "This curve determines the weight attenuation over the distance of the mesh.\nThe selected polygroup handle is t=0.0, and t=1.0 is roughly the farthest vertices from the handles.\nThe value of the curve at each time interval represents the weight of the vertices at that distance from the selection."))
	FRuntimeFloatCurve WeightAttenuationCurve;
*/ 
};

//Stores per-vertex data needed by the laplacian deformer object
//TODO: May be a candidate for a subclass of the FGroupTopologyLaplacianDeformer
struct FConstraintData
{
	FConstraintData& operator=(const FConstraintData& other)
	{
		Position = other.Position;
		Weight   = other.Weight;
		bPostFix = other.bPostFix;
		return *this;
	}

	FVector3d Position;
	double Weight{ 0.0 };
	bool bPostFix{ false };
};

/**
*	FDeformTask is an object which wraps an asynchronous task to be run multiple times on a separate thread.
*	The Laplacian deformation process requires the use of potentially large sparse matrices and sparse multiplication.
*	
*   Expected usage:
*
*   
*   // define constraints.  Need Constraints[VertID] to hold the constraints for the corresponding vertex.
*   TArray<FConstraintData> Constraints;
*   ....
*
*   // populate with the VertexIDs of the vertices that are in the region you wish to deform.  
*   TArray<int32> SrcVertIDs;  //Basically a mini-index buffer.
*   ...
* 
*   // Create or reuse a laplacian deformation task.
*   FDeformTask*   DeformTask = New FDeformTask(WeightScheme);
*
*   // the deformer will have to build a new mesh that represents the regions in SrcVertIDs;
*   // but set this to false on subsequent calls to UpdateDeformer if the SrcVertIDs array hasn't changed.
*   bool bRequiresRegion = true;

*   DefTask->UpdateDeformer(WeightScheme, Mesh, Constraints, SrcVertIDs, bRequiresRegion);
*
*   DeformTask->DoWork();  or DeformTask->StartBackgroundTask(); //which calls DoWork on background thread.
*
*  // wheh DeformTask->IsDone == true; you can copy the results back to the mesh
*  DeformTask->ExportResults(Mesh);
*
* Note: if only the positions in the Constraints change (e.g. handle positions) then subsequent calls 
* to UpdateDeformer() and DoWork() will be much faster as the matrix system will not be rebuilt or re-factored
*/
class FConstrainedMeshDeformerTask : public FNonAbandonableTask
{
	friend class FAsyncTask<FDeformTask>;
public:

	enum
	{
		INACTIVE_SUBSET_ID = -1
	};

	FConstrainedMeshDeformerTask(const ELaplacianWeightScheme SelectedWeightScheme)
	{
		LaplacianWeightScheme = SelectedWeightScheme;
	}

	virtual ~FConstrainedMeshDeformerTask() {};

	//NO idea what this is meant to do. Performance analysis maybe? Scheduling data?
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FConstrainedMeshDeformerTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	
	/** Called by the main thread in the tool, this copies the Constraint buffer right before the task begins on another thread.
	  * Ensures the FConstrainedMeshDeformer is using correct mesh subset and the selected settings, then updates on change in properties, i.e. weight scheme */
	void UpdateDeformer(const ELaplacianWeightScheme SelectedWeightScheme, const FDynamicMesh3& Mesh, const TArray<FConstraintData>& ConstraintArray, const TArray<int32>& SrcIDBufferSubset, bool bNewTransaction, const FRichCurve* Curve);

	/** Required by the FAsyncTaskExecutor */
	void SetAbortSource(bool* bAbort) { bAbortSource = bAbort; };

	/** Called by the FAsyncTask<FDeformTask> object for background computation. */
	void DoWork();


	/** Updates the positions in the target mesh for regions that correspond to the subset mesh */
	void ExportResults(FDynamicMesh3& TargetMesh) const;

private:

	/** Creates the mesh (i.e. SubsetMesh) that corresponds to the region of the SrcMesh defined by the partial index buffer SrcIDBufferSubset */
	void InitializeSubsetMesh(const FDynamicMesh3& SrcMesh, const TArray<int32>& SrcIDBufferSubset);

	/** Attenuates the weights of the constraints using the selected curve */
	void ApplyAttenuation();

	/** Denotes the weight scheme being used by the running background task. Changes when selected property changes in editor. */
	ELaplacianWeightScheme LaplacianWeightScheme;

	/** positions for each vertex in the subset mesh - for use in the deformer */
	TArray<FVector3d> SubsetPositionBuffer;

	/** constraint data for each vertex in subset mesh - for use by the deformer*/
	TArray<FConstraintData> SubsetConstraintBuffer;

	FRichCurve WeightAttenuationCurve;

	/** True only for the first update, and then false for the duration of the Input transaction
	  * It's passed in and copied in UpdateDeformer() */
	bool bIsNewTransaction = true;

	/** When true, the constraint weights will be attenuated based on distance using the provided curve object*/
	bool bAttenuateWeights = false;

	/** The abort bool used by the Task Deleter */
	bool* bAbortSource = nullptr;

	/** Used to initialize the array mapping, updated during the UpdateDeformer() function */
	int SrcMeshMaxVertexID;

	/** A subset of the original mesh */
	FDynamicMesh3 SubsetMesh;

	/** Maps Subset Mesh VertexID to Src Mesh VertexID */
	TArray<int32> SubsetVertexIDToSrcVertexIDMap;

	/** Laplacian deformer object gets rebuilt each new transaction */
	TUniquePtr<FConstrainedMeshDeformer> ConstrainedDeformer;

private :
	FConstrainedMeshDeformerTask();

};

class FGroupTopologyLaplacianDeformer : public FGroupTopologyDeformer
{

public:

	FGroupTopologyLaplacianDeformer() = default;

	virtual ~FGroupTopologyLaplacianDeformer();

	/** Used to begin a procedural addition of modified vertices */
	inline void ResetModifiedVertices()
	{
		ModifiedVertices.Empty();
	};

	/** Change tracking */
	template <typename ValidSetAppendContainerType>
	void RecordModifiedVertices(const ValidSetAppendContainerType& Container)
	{
		ModifiedVertices.Empty();
		ModifiedVertices.Append(Container);
	}

	/** Used to iteratively add to the active change set (TSet<>)*/
	inline void RecordModifiedVertex(int32 VertexID)
	{
		ModifiedVertices.Add(VertexID);
	};


	void SetActiveHandleFaces(const TArray<int>& FaceGroupIDs) override;
	void SetActiveHandleEdges(const TArray<int>& TopologyEdgeIDs) override;
	void SetActiveHandleCorners(const TArray<int>& TopologyCornerIDs) override;



	/** Allocates shared storage for use in task synchronization */
	void InitBackgroundWorker(const ELaplacianWeightScheme WeightScheme);


	/** Coordinates the background tasks. Returns false if the worker was already running */
	bool UpdateAndLaunchdWorker(const ELaplacianWeightScheme WeightScheme, const FRichCurve* Curve = nullptr);

	/** Capture data about background task state.*/
	bool IsTaskInFlight() const;



	/** Sets the SrcMeshConstraintBuffer to have a size of MaxVertexID, and initializes with the current mesh positions, but weight zero*/
	void InitializeConstraintBuffer();

	/** Given an array of Group IDs, update the selection and record vertices */
	void UpdateSelection(const FDynamicMesh3* TargetMesh, const TArray<int>& Groups, bool bLocalizeDeformation);

	/** Updates the mesh preview and/or solvers upon user input, provided a deformation strategy */
	void UpdateSolution(FDynamicMesh3* TargetMesh, const TFunction<FVector3d(FDynamicMesh3* Mesh, int)>& HandleVertexDeformFunc) override;

	/** Updates the vertex positions of the mesh with the result from the last deformation solve. */
	void ExportDeformedPositions(FDynamicMesh3* TargetMesh);

	/** Returns true if the asynchronous task has finished. */
	inline bool IsDone() { return AsyncMeshDeformTask == nullptr || AsyncMeshDeformTask->IsDone(); };

	/** Triggers abort on task and passes off ownership to deleter object */
	inline void Shutdown();

	const TArray<FROIFace>& GetROIFaces() const { return ROIFaces; }

	/** Stores the position of the vertex constraints and corresponding weights for the entire mesh.  This is used as a form of scratch space.*/
	TArray<FConstraintData> SrcMeshConstraintBuffer;

	/** Array of vertex indices organized in groups of three - basically an index buffer - that defines the subset of the mesh that the deformation task will work on.*/
	TArray<int32> SubsetIDBuffer;

	/** Need to update the task with the current submesh */
	bool bTaskSubmeshIsDirty = true;

	/** Asynchronous task object. This object deals with expensive matrix functionality that computes the deformation of a local mesh. */
	FAsyncTaskExecuterWithAbort<FConstrainedMeshDeformerTask>* AsyncMeshDeformTask = nullptr;


	/** The weight which will be applied to the constraints corresponding to the handle vertices. */
	double HandleWeights = 1.0;

	/** This is set to true whenever the user interacts with the tool under laplacian deformation mode.
	  * It is set to false immediately before beginning a background task and cannot be set to false again until the work is done. */
	bool bDeformerNeedsToRun = false;


	/** When true, tells the solver to attempt to postfix the actual position of the handles to the constrained position */
	bool bPostfixHandles = false;

	//This is set to false only after 
	//	1) the asynchronous deformation task is complete
	//	2) the main thread has seen it complete, and
	//	3) the main thread updates the vertex positions of the mesh one last time
	bool bVertexPositionsNeedSync = false;

	bool bLocalize =  true;

};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UEditMeshPolygonsTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }


	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

public:
	virtual void NextTransformTypeAction();

	//
	float VisualAngleSnapThreshold = 0.5;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UPolyEditTransformProperties* TransformProps;

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property);
	
	// camera state at last render
	FViewCameraState CameraState;

	FToolDataVisualizer PolyEdgesRenderer;

	// True for the duration of UI click+drag
	bool bInDrag;

	FPlane ActiveDragPlane;
	FVector StartHitPosWorld;
	FVector StartHitNormalWorld;
	FVector LastHitPosWorld;
	FVector LastBrushPosLocal;
	FVector StartBrushPosLocal;

	FFrame3d ActiveSurfaceFrame;
	FQuickTransformer* GetActiveQuickTransformer();
	void UpdateActiveSurfaceFrame(FGroupTopologySelection& Selection);
	void UpdateQuickTransformer();

	FRay UpdateRay;
	bool bUpdatePending = false;
	void ComputeUpdate();

	FVector3d LastMoveDelta;
	FQuickAxisTranslater QuickAxisTranslater;
	void ComputeUpdate_Translate();

	FQuickAxisRotator QuickAxisRotator;
	FVector3d RotationStartPointWorld;
	FFrame3d RotationStartFrame;
	void ComputeUpdate_Rotate();


	FGroupTopology Topology;
	void PrecomputeTopology();
	void ComputePolygons(bool RecomputeTopology = true);

	FGroupTopologySelector TopoSelector;
	
	//
	// data for current drag
	//

	FGroupTopologySelection HilightSelection;
	FToolDataVisualizer HilightRenderer;

	FDynamicMeshAABBTree3 MeshSpatial;
	FDynamicMeshAABBTree3& GetSpatial();

	FMeshVertexChangeBuilder* ActiveVertexChange;

	// The two deformer type options.
	FGroupTopologyDeformer LinearDeformer;
	FGroupTopologyLaplacianDeformer LaplacianDeformer;

	EGroupTopologyDeformationStrategy DeformationStrategy;

	// Initial polygon group and mesh info
	TDynamicVector<int> InitialTriangleGroups;
	TUniquePtr<FDynamicMesh3> InitialMesh;
	void BackupTriangleGroups();
	void SetTriangleGroups(const TDynamicVector<int>& Groups);
	
	// This is true when the spatial index needs to reflect a modification
	bool bSpatialDirty; 

	void BeginChange();
	void EndChange();
	void UpdateChangeFromROI(bool bFinal);

};

