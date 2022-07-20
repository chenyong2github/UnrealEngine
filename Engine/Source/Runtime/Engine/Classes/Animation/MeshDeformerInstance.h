// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDeformerInstance.generated.h"

class FSceneInterface;

UCLASS(Abstract, EditInlineNew)
class ENGINE_API UMeshDeformerInstanceSettings : public UObject
{
	GENERATED_BODY()
};

/** 
 * Base class for mesh deformers instances.
 * This contains the per instance state for a UMeshDeformer.
 */
UCLASS(Abstract)
class ENGINE_API UMeshDeformerInstance : public UObject
{
	GENERATED_BODY()

public:
	/** Enumeration for workloads to enqueue. */
	enum EWorkLoad
	{
		WorkLoad_Setup,
		WorkLoad_Trigger,
		WorkLoad_Update,
	};

	/** Called to allocate any persistent render resources */
	virtual void AllocateResources() {}

	/** Called when persistent render resources should be released */ 
	virtual void ReleaseResources() {}

	/** Get if mesh deformer is active (compiled and valid). */
	virtual bool IsActive() const PURE_VIRTUAL(, return false;);
	/** Enqueue the mesh deformer workload on a scene. */
	virtual void EnqueueWork(FSceneInterface* InScene, EWorkLoad InWorkLoadType, FName InOwnerName) PURE_VIRTUAL(, );
};
