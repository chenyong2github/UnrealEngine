// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshActor.h"

#include "GeneratedDynamicMeshActor.generated.h"

class AStaticMeshActor;


/**
 * AGeneratedDynamicMeshActor is an Editor-only subclass of ADynamicMeshActor that provides 
 * special support for dynamic procedural generation of meshes in the Editor, eg via Blueprints. 
 * Expensive procedural generation implemented via BP can potentially cause major problems in 
 * the Editor, in particular with interactive performance. AGeneratedDynamicMeshActor provides
 * special infrastructure for this use case. Essentially, instead of doing procedural generation
 * in the Construction Script, a BP-implementable event OnRebuildGeneratedMesh is available,
 * and doing the procedural mesh regeneration when that function fires will generally provide
 * better in-Editor interactive performance.
 */
UCLASS()
class GEOMETRYSCRIPTINGEDITOR_API AGeneratedDynamicMeshActor : public ADynamicMeshActor
{
	GENERATED_UCLASS_BODY()

public:

	virtual ~AGeneratedDynamicMeshActor();


public:
	/** If true, the internal UDynamicMesh will be cleared before the OnRebuildGeneratedMesh event is fired. */
	UPROPERTY(Category = DynamicMeshActor, EditAnywhere, BlueprintReadWrite)
	bool bResetOnRebuild = true;


	/**
	 * This event will be fired to notify the BP that the generated Mesh should
	 * be rebuilt. GeneratedDynamicMeshActor BP subclasses should rebuild their 
	 * meshes on this event, instead of doing so directly from the Construction Script.
	 */
	UFUNCTION(BlueprintImplementableEvent, CallInEditor, Category = "Events")
	void OnRebuildGeneratedMesh(UDynamicMesh* TargetMesh);


	/**
	 * This function will fire the OnRebuildGeneratedMesh event if the actor has been
	 * marked for a pending rebuild (eg via OnConstruction)
	 */
	virtual void ExecuteRebuildGeneratedMeshIfPending();


public:

	/** 
	 * Attempt to copy Actor Properties to a StaticMeshActor. Optionally copy DynamicMeshComponent material list to the StaticMeshComponent.
	 * This function is useful when (eg) swapping from a DynamicMeshActor to a StaticMeshActor as it will allow
	 * many configured Actor settings to be preserved (like assigned DataLayers, etc)
	 */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	void CopyPropertiesToStaticMesh(AStaticMeshActor* StaticMeshActor, bool bCopyComponentMaterials = false);

	/**
	 * Attempt to copy Actor Properties from a StaticMeshActor. Optionally copy DynamicMeshComponent material list to the StaticMeshComponent.
	 * This function is useful when (eg) swapping from a StaticMeshActor to a DynamicMeshActor as it will allow
	 * many configured Actor settings to be preserved (like assigned DataLayers, etc) 
	 */
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
	void CopyPropertiesFromStaticMesh(AStaticMeshActor* StaticMeshActor, bool bCopyComponentMaterials = false);



public:

	// AActor overrides
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;


protected:
	// this internal flag is set in OnConstruction, and will cause ExecuteRebuildGeneratedMesh to
	// fire the OnRebuildGeneratedMesh event, after which the flag will be cleared
	bool bGeneratedMeshRebuildPending = false;

	// indicates that this Actor is registered with the UEditorGeometryGenerationSubsystem, which 
	// is where the mesh rebuilds are executed
	bool bIsRegisteredWithGenerationManager = false;

	virtual void RegisterWithGenerationManager();
	virtual void UnregisterWithGenerationManager();
};
