// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef GEOMETRYCOLLECTION_DEBUG_DRAW
#define GEOMETRYCOLLECTION_DEBUG_DRAW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

#include "Components/MeshComponent.h"

#include "GeometryCollectionDebugDrawComponent.generated.h"

#if INCLUDE_CHAOS && GEOMETRYCOLLECTION_DEBUG_DRAW
#include "GeometryCollection/GeometryCollectionParticlesData.h"
#endif  // #if INCLUDE_CHAOS && GEOMETRYCOLLECTION_DEBUG_DRAW

class AGeometryCollectionRenderLevelSetActor;
class UGeometryCollectionComponent;
class AGeometryCollectionDebugDrawActor;
class AChaosSolverActor;

#if INCLUDE_CHAOS && GEOMETRYCOLLECTION_DEBUG_DRAW
namespace Chaos { template<class T, int d> class TImplicitObject; }
namespace Chaos { template<class T, int d> class TPBDRigidParticles; }
#endif  // #if INCLUDE_CHAOS && GEOMETRYCOLLECTION_DEBUG_DRAW

/**
* FGeometryCollectionDebugDrawWarningMessage
*   Empty structure used to embed a warning message in the UI through a detail customization.
*/
USTRUCT()
struct FGeometryCollectionDebugDrawWarningMessage
{
	GENERATED_USTRUCT_BODY()
};

/**
* UGeometryCollectionDebugDrawComponent
*   Component adding debug drawing functionality to a GeometryCollectionActor.
*   This component is automatically added to every GeometryCollectionActor.
*/
UCLASS(meta = (BlueprintSpawnableComponent), HideCategories = ("Tags", "Activation", "Cooking", "AssetUserData", "Collision"))
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionDebugDrawComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:

	/** Warning message to explain that the debug draw properties have no effect until starting playing/simulating. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw")
	FGeometryCollectionDebugDrawWarningMessage WarningMessage;

	/** Enable the visualization of the rigid body ids. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyIds;

	/** Enable rigid body transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyTransforms;

	/** Enable the visualization of the implicit collision primitives (excluding level sets). */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyCollisions;

	/** Enable rigid body inertia tensor box visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyInertias;

	/** Enable rigid body velocities visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyVelocities;

	/** Enable rigid body applied force and torque visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body")
	bool bShowRigidBodyForces;

	/** Enable the visualization of the rigid body infos. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body", AdvancedDisplay)
	bool bShowRigidBodyInfos;

	/** Color used for the visualization of the rigid body ids. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyIdColor;

	/** Scale for rigid body transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (ClampMin="0.0001"))
	float RigidBodyTransformScale;

	/** Color used for collision primitives visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyCollisionColor;

	/** Color used for the visualization of the rigid body inertia tensor box. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyInertiaColor;

	/** Color used for rigid body velocities visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyVelocityColor;

	/** Color used for rigid body applied force and torque visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyForceColor;

	/** Color used for the visualization of the rigid body infos. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Rigid Body Settings", meta = (HideAlphaChannel))
	FColor RigidBodyInfoColor;

	/** Enable the visualization of the transform indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering")
	bool bShowTransformIndices;

	/** Enable cluster transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering")
	bool bShowTransforms;

	/** Enable the visualization of the cluster levels. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering")
	bool bShowLevels;

	/** Enable the visualization of the link from the parents. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering")
	bool bShowParents;

	/** Enable the visualization of the rigid clustering connectivity edges. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering")
	bool bShowConnectivityEdges;

	/** Color used for the visualization of the transform indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering Settings", meta = (HideAlphaChannel))
	FColor TransformIndexColor;

	/** Scale for cluster transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering Settings", meta = (ClampMin="0.0001"))
	float TransformScale;

	/** Color used for the visualization of the levels. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering Settings", meta = (HideAlphaChannel))
	FColor LevelColor;

	/** Color used for the visualization of the link from the parents. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering Settings", meta = (HideAlphaChannel))
	FColor ParentColor;

	/** Line thickness used for the visualization of the rigid clustering connectivity edges. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Clustering Settings", meta = (ClampMin="0.0001"))
	float ConnectivityEdgeThickness;

	/** Enable the visualization of the geometry indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowGeometryIndices;

	/** Enable geometry transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowGeometryTransforms;

	/** Enable the visualization of the bounding boxes. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowBoundingBoxes;

	/** Enable the visualization of faces. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowFaces;

	/** Enable the visualization of the face indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowFaceIndices;

	/** Enable the visualization of the face normals. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowFaceNormals;

	/** Enable single face visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowSingleFace;

	/** Index of the single face to visualize. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry", meta = (ClampMin="0"))
	int32 SingleFaceIndex;

	/** Enable the visualization of the vertices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowVertices;

	/** Enable the visualization of the vertex indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowVertexIndices;

	/** Enable the visualization of the vertex normals. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry")
	bool bShowVertexNormals;

	/** Color used for the visualization of the geometry indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor GeometryIndexColor;

	/** Scale for geometry transform visualization. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (ClampMin="0.0001"))
	float GeometryTransformScale;

	/** Color used for the visualization of the bounding boxes. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor BoundingBoxColor;

	/** Color used for the visualization of the faces. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor FaceColor;

	/** Color used for the visualization of the face indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor FaceIndexColor;

	/** Color used for the visualization of the face normals. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor FaceNormalColor;

	/** Color used for the visualization of the single face. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor SingleFaceColor;

	/** Color used for the visualization of the vertices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor VertexColor;

	/** Color used for the visualization of the vertex indices. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor VertexIndexColor;

	/** Color used for the visualization of the vertex normals. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw|Geometry Settings", meta = (HideAlphaChannel))
	FColor VertexNormalColor;

	/** Singleton actor, containing the debug draw properties. Automatically populated at play time unless explicitely set. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw", AdvancedDisplay)
	AGeometryCollectionDebugDrawActor* GeometryCollectionDebugDrawActor;

	/** Level Set singleton actor, containing the Render properties. Automatically populated at play time unless explicitely set. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw", AdvancedDisplay)
	AGeometryCollectionRenderLevelSetActor* GeometryCollectionRenderLevelSetActor;

	UGeometryCollectionComponent* GeometryCollectionComponent;  // the component we are debug rendering for, set by the GeometryCollectionActor after creation

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type ReasonEnd) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	
#if WITH_EDITOR
	/** Property changed callback. Used to clamp the level set and single face index properties. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Some properties cab be locked depending on the value of the debug draw actor. */
	virtual bool CanEditChange(const UProperty* InProperty) const override;
#endif

#if GEOMETRYCOLLECTION_DEBUG_DRAW
	/** Update selection and visibility after any change in properties. Also enable/disable this component tick update. Return true if this geometry collection is selected. */
	bool OnDebugDrawPropertiesChanged(bool bForceVisibilityUpdate);

	/** Update selection and visibility after a change in cluster. Only handled when the debug drawing is active (the component is ticking). */
	void OnClusterChanged();

	/** Return whether the geometry collection rigid body id array is not completely initialized. This can happen when running the physics multithreaded. */
	FORCEINLINE bool HasIncompleteRigidBodyIdSync() const { return bHasIncompleteRigidBodyIdSync;  }

private:
	/** Recursively compute global cluster transforms. Only gives geometry transforms for the leaf nodes, mid-level transforms are those of the clusters. */
	void ComputeClusterTransforms(int32 Index, TArray<bool>& IsComputed, TArray<FTransform>& InOutGlobalTransforms);

	/** 
	* Compute global transforms.
	* Unlike GeometryCollectionAlgo::GlobalMatrices(), this also calculates the correct mid-level geometry transforms and includes the actor transform.
	*/
	void ComputeTransforms(TArray<FTransform>& OutClusterTransforms, TArray<FTransform>& OutGeometryTransforms);

	/** Geometry collection debug draw. */
	void DebugDrawTick();

	/** Update the transform index dependending on the current filter settings. */
	void UpdateSelectedTransformIndex();

	/** Return the number of faces for the given geometry (includes its children, and includes its detached children when bDebugDrawClustering is true). */
	int32 CountFaces(int32 TransformIndex, bool bDebugDrawClustering) const;

	/** Update visible array to hide the selected geometry and its children, and includes its detached children when bDebugDrawClustering is true)*/
	void HideFaces(int32 TransformIndex, bool bDebugDrawClustering);

	/** Update geometry visibility. Set bForceVisibilityUpdate to true to force the visibility array update. */
	void UpdateGeometryVisibility(bool bForceVisibilityUpdate = false);

	/** Update ticking status. */
	void UpdateTickStatus();

#if INCLUDE_CHAOS
	/** Chaos dependent debug draw. */
	void DebugDrawChaosTick();

	/** Update level set visibility. */
	void UpdateLevelSetVisibility();
#endif  // #if INCLUDE_CHAOS
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW

private:
	static UGeometryCollectionDebugDrawComponent* RenderLevelSetOwner;
	static int32 LastRenderedId;

#if GEOMETRYCOLLECTION_DEBUG_DRAW
#if INCLUDE_CHAOS
	FGeometryCollectionParticlesData ParticlesData;
#endif  // #if INCLUDE_CHAOS
	int32 ParentCheckSum;
	int32 SelectedRigidBodyId;
	int32 SelectedTransformIndex;
	int32 HiddenTransformIndex;
	bool bWasVisible;
	bool bHasIncompleteRigidBodyIdSync;
	AChaosSolverActor* SelectedChaosSolver;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
};
