// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"

#include "DynamicMeshActor.generated.h"


/**
 * ADynamicMeshActor is an Actor that has a USimpleDynamicMeshComponent as it's RootObject.
 */
UCLASS(ConversionRoot, ComponentWrapperClass, ClassGroup=DynamicMesh, meta = (ChildCanTick))
class GEOMETRYFRAMEWORK_API ADynamicMeshActor : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(Category = DynamicMeshActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	TObjectPtr<class UDynamicMeshComponent> DynamicMeshComponent;

public:
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	UDynamicMeshComponent* GetDynamicMeshComponent() const { return DynamicMeshComponent; }



	//
	// Mesh Pool support. Meshes can be locally allocated from the Mesh Pool
	// in Blueprints, and then released back to the Pool and re-used. This
	// avoids generating temporary UDynamicMesh instances that need to be
	// garbage-collected. See UDynamicMeshPool for more details.
	//

public:
	/** Control whether the DynamicMeshPool will be created when requested via GetComputeMeshPool() */
	UPROPERTY(Category = DynamicMeshActor, VisibleAnywhere, BlueprintReadWrite)
	bool bEnableComputeMeshPool = true;
private:
	/** The internal Mesh Pool, for use in DynamicMeshActor BPs. Use GetComputeMeshPool() to access this, as it will only be created on-demand if bEnableComputeMeshPool = true */
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshPool> DynamicMeshPool;

public:
	/** Access the compute mesh pool */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	UDynamicMeshPool* GetComputeMeshPool();

	/** Request a compute mesh from the Pool, which will return a previously-allocated mesh or add and return a new one. If the Pool is disabled, a new UDynamicMesh will be allocated and returned. */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	UDynamicMesh* AllocateComputeMesh();

	/** Release a compute mesh back to the Pool */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	bool ReleaseComputeMesh(UDynamicMesh* Mesh);

	/** Release all compute meshes that the Pool has allocated */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	void ReleaseAllComputeMeshes();

	/** Release all compute meshes that the Pool has allocated, and then release them from the Pool, so that they will be garbage-collected */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	void FreeAllComputeMeshes();




	//
	// In-Editor Generated/Procedural Mesh Actor support. 
	// 
	// These members are used to help with the creation of DynamicMeshActors that generate
	// meshes dynamically in the Editor, for example via Blueprints. Expensive procedural 
	// generation via BP can potentially cause major problems in the Editor, and so the
	// implmentor of a BP Actor mesh generator needs to take some care.
	// 
	// If bIsEditorGeneratedMeshActor = true, then OnEditorRebuildGeneratedMesh
	// will fire after the Construction script runs for this Actor. OnEditorRebuildGeneratedMesh does not directly
	// fire from the Construction Script, but rather in the next Tick. This works around a fundamental
	// limitation related to "mouse event priority" and slider/transform input in the UE Editor.
	// Essentially doing even a moderately expensive mesh generation operation directly in the 
	// Construction Script will appear to freeze the Viewport, while doing the same operation in the
	// OnEditorRebuildGeneratedMesh implementation will remain responsive (albeit at a low FPS rate).
	// 
	// In future this may be extended to (eg) throttle procedural regeneration to further improve
	// interactivity. 
	// 
	// Note that the current implementation requires in-Editor Ticking for this Actor. If bIsEditorGeneratedMeshActor
	// is true, then ShouldTickIfViewportsOnly() will return true, which means that the Actor Tick Event
	// will also fire in Editor viewports. This means that any game logic wired to the Tick Event
	// in the Actor BP will aso run outside of PIE!
	// 
	// Note that the above behavior currently only occurs in the Editor, where the Construction script 
	// is frequently re-executed (on Actor Transform and Property Editing). 
	//
public:

	/**
	 * Set this flag to true in a BP Subclass of DynamicMeshActor that procedurally generates it's
	 * own mesh via BP (eg with Geometry Script). This will cause OnEditorRebuildGeneratedMesh to fire after 
	 * the Construction Script is run or re-run. Regenerating the mesh on this event, rather than directly
	 * in the Construction Script, will result in better interactive performance in the Editor.
	 */
	UPROPERTY(EditAnywhere, Category = DynamicMeshActor )
	bool bIsEditorGeneratedMeshActor = false;

	/**
	 * This event will fire from Tick() to notify listeners that the internal Mesh should
	 * be rebuilt. Procedural Mesh Generator Actors built in BP's derived from DynamicMeshActor
	 * should listen for this event and use it to rebuild their meshes, instead of doing so
	 * directly from the Construction Script.
	 */
	UFUNCTION(BlueprintImplementableEvent, CallInEditor, Category = "Events")
	void OnEditorRebuildGeneratedMesh();



	// the functions below are overridden to implement the above behavior
public:
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	// this internal flag is set in OnConstruction if bIsEditorGeneratedMeshActor=true, and will cause
	// OnEditorRebuildGeneratedMesh to fire in Tick(), after which the flag is cleared
	bool bGeneratedMeshRebuildPending = false;
};


