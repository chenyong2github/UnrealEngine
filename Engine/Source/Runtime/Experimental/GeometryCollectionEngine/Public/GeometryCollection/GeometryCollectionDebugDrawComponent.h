// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef GEOMETRYCOLLECTION_DEBUG_DRAW
#define GEOMETRYCOLLECTION_DEBUG_DRAW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

#include "Components/MeshComponent.h"

#include "GeometryCollectionDebugDrawComponent.generated.h"

#if GEOMETRYCOLLECTION_DEBUG_DRAW
#include "GeometryCollection/GeometryCollectionParticlesData.h"
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW

class AGeometryCollectionRenderLevelSetActor;
class UGeometryCollectionComponent;
class AGeometryCollectionDebugDrawActor;
class AChaosSolverActor;

#if GEOMETRYCOLLECTION_DEBUG_DRAW
namespace Chaos { class FImplicitObject; }
namespace Chaos { template<class T, int d> class TPBDRigidParticles; }
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW

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

	/** Singleton actor, containing the debug draw properties. Automatically populated at play time unless explicitly set. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw", AdvancedDisplay)
	TObjectPtr<AGeometryCollectionDebugDrawActor> GeometryCollectionDebugDrawActor;

	/** Level Set singleton actor, containing the Render properties. Automatically populated at play time unless explicitly set. */
	UPROPERTY(EditAnywhere, Category = "Debug Draw", AdvancedDisplay)
	TObjectPtr<AGeometryCollectionRenderLevelSetActor> GeometryCollectionRenderLevelSetActor;

	UGeometryCollectionComponent* GeometryCollectionComponent;  // the component we are debug rendering for, set by the GeometryCollectionActor after creation

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type ReasonEnd) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	
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

	/** Chaos dependent debug draw. */
	void DebugDrawChaosTick();

	/** Update level set visibility. */
	void UpdateLevelSetVisibility();
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW

private:
	static UGeometryCollectionDebugDrawComponent* RenderLevelSetOwner;
	static int32 LastRenderedId;
	//static FGuid LastRenderedId;

#if GEOMETRYCOLLECTION_DEBUG_DRAW
	FGeometryCollectionParticlesData ParticlesData;
	int32 ParentCheckSum;
	int32 SelectedRigidBodyId;
	//FGuid SelectedRigidBodyId;
	int32 SelectedTransformIndex;
	int32 HiddenTransformIndex;
	bool bWasVisible;
	bool bHasIncompleteRigidBodyIdSync;
	AChaosSolverActor* SelectedChaosSolver;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
};
